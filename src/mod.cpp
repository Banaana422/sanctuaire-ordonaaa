// Donjons du Crepuscule - v1.1
//
//  * 3 teleporteurs (vert = Foret, rouge = Volcan, bleu = Lac) qui envoient dans les 3 premiers temples
//    par le changement de stage natif du jeu (comme le Boss Rush de Twilit Essentials).
//  * En sortant du donjon, on revient la ou on etait avant de se teleporter.
//  * Onglet "Crepuscule" dans le menu F1 : teleportation, reglages, debogage, spawn de monstres.
//  * Mode difficile : monstres supplementaires dans chaque salle du donjon.
//  * Teinte sombre "crepuscule" dans le donjon (approximation : assombrit l'ambiance du jeu).
//
// Pas de combinaison de touches : A pres d'un portail, ou le menu F1.
//
// ATTENTION : non teste en jeu. Les valeurs marquees "A VERIFIER" sont des hypotheses.

// IMPORTANT : en-tetes standard AVANT ceux du jeu (sinon erreur C2440 avec Visual Studio).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/host.h"
#include "mods/svc/log.hpp"
#include "mods/svc/ui.h"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_controller_pad.h"

#include "proc_names.hpp"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(UiService, svc_ui);

namespace {

// =============================================================================================
//  Donnees
// =============================================================================================

struct Dungeon {
    const char* label;   // nom affiche
    const char* color;   // couleur du portail
    const char* prefix;  // 6 premieres lettres du stage (les salles de boss ont des suffixes : D_MN05A...)
    int defPoint;        // point d'arrivee   (A VERIFIER)
    int defRoom;         // salle d'arrivee   (A VERIFIER)
};

const Dungeon kDungeons[3] = {
    {"Temple de la Forêt", "vert", "D_MN05", 0, 0},    // A VERIFIER : stage
    {"Mines des Gorons", "rouge", "D_MN04", 0, 0},     // A VERIFIER : stage
    {"Temple du Lac", "bleu", "D_MN01", 0, 0},         // A VERIFIER : stage
};

struct MobDef {
    const char* label;
    int proc;       // fpcNm_E_xxx_e
    uint32_t params;
    int sizePct;    // 0 = utiliser le reglage "Taille" du menu
};

// Identifiants d'acteurs ennemis du jeu. Les noms sont a identifier en jeu avec le menu de debogage.
const MobDef kMobs[] = {
    {"E_BA", fpcNm_E_BA_e, 0xFFFFFFFFu}, {"E_KG", fpcNm_E_KG_e, 0xFFFFFFFFu},
    {"E_GB", fpcNm_E_GB_e, 0xFFFFFFFFu}, {"E_WS", fpcNm_E_WS_e, 0xFFFFFFFFu},
    {"E_DN", fpcNm_E_DN_e, 0xFFFFFFFFu}, {"E_S1", fpcNm_E_S1_e, 0xFFFFFFFFu},
    {"E_SF", fpcNm_E_SF_e, 0xFFFFFFFFu}, {"E_OC", fpcNm_E_OC_e, 0xFFFFFFFFu},
    {"E_HB", fpcNm_E_HB_e, 0xFFFFFFFFu}, {"E_MK", fpcNm_E_MK_e, 0xFFFFFFFFu},
    {"E_PO", fpcNm_E_PO_e, 0xFFFFFFFFu}, {"E_TT", fpcNm_E_TT_e, 0xFFFFFFFFu},
    {"E_YM", fpcNm_E_YM_e, 0xFFFFFFFFu}, {"E_GM", fpcNm_E_GM_e, 0xFFFFFFFFu},
    {"E_BI", fpcNm_E_BI_e, 0xFFFFFFFFu}, {"E_RB", fpcNm_E_RB_e, 0xFFFFFFFFu},
    {"E_ST", fpcNm_E_ST_e, 0xFFFFFFFFu}, {"E_TK", fpcNm_E_TK_e, 0xFFFFFFFFu},
    {"E_SB", fpcNm_E_SB_e, 0xFFFFFFFFu}, {"E_ZH", fpcNm_E_ZH_e, 0xFFFFFFFFu},
    {"E_ZM (masque ?)", fpcNm_E_ZM_e, 0xFFFFFFFFu},
    {"E_DB (boss Diababa : experimental)", fpcNm_E_DB_e, 0xFFFFFFFFu},
    // ---- Monstres personnalises : modeles du jeu, taille modifiee (vie/degats/couleur : etape suivante)
    {"Perso : Géant E_GB (x2.2)", fpcNm_E_GB_e, 0xFFFFFFFFu, 220},
    {"Perso : Minuscule E_BA (x0.4)", fpcNm_E_BA_e, 0xFFFFFFFFu, 40},
    {"Perso : Colosse E_DN (x2.5)", fpcNm_E_DN_e, 0xFFFFFFFFu, 250},
    {"Perso : Titan E_WS (x3)", fpcNm_E_WS_e, 0xFFFFFFFFu, 300},
    {"Perso : Brute E_S1 (x1.8)", fpcNm_E_S1_e, 0xFFFFFFFFu, 180},
    {"Perso : Grand E_HB (x1.6)", fpcNm_E_HB_e, 0xFFFFFFFFu, 160},
    {"Perso : Mini boss E_DB (x0.35)", fpcNm_E_DB_e, 0xFFFFFFFFu, 35},
};
constexpr int kMobCount = static_cast<int>(sizeof(kMobs) / sizeof(kMobs[0]));

struct Portal {
    bool set = false;
    std::string stage;
    int room = 0;
    float x = 0, y = 0, z = 0;
};

struct Origin {
    bool valid = false;
    std::string stage;
    int room = 0;
    float x = 0, y = 0, z = 0;
    s16 yaw = 0;
};

enum Phase { PH_NONE, PH_GOING, PH_INSIDE, PH_RETURNING, PH_ARRIVED };

// =============================================================================================
//  Etat
// =============================================================================================

// Variables de configuration (sauvegardees par Dusklight, visibles dans le menu)
ConfigVarHandle v_unlockAll{}, v_hard{}, v_extra{}, v_size{}, v_tint{}, v_hardMob{}, v_tintPct{};
ConfigVarHandle v_unlock[3]{};
ConfigVarHandle v_pSet[3]{}, v_pStage[3]{}, v_pRoom[3]{}, v_pX[3]{}, v_pY[3]{}, v_pZ[3]{};
ConfigVarHandle v_dPoint[3]{}, v_dRoom[3]{};

// Stages de donjons proposes dans le menu (pour corriger une destination sans recompiler)
const char* const kStageChoices[] = {"D_MN01", "D_MN04", "D_MN05", "D_MN06", "D_MN07", "D_MN08", "D_MN09",
    "D_MN10", "D_MN11"};
constexpr int kStageChoiceCount = static_cast<int>(sizeof(kStageChoices) / sizeof(kStageChoices[0]));
const int kDefStageIdx[3] = {2, 1, 0};  // D_MN05, D_MN04, D_MN01
ConfigVarHandle v_dStage[3]{};

Portal g_portal[3];
Origin g_origin;
Phase g_phase = PH_NONE;
int g_dun = -1;
int g_phaseFrames = 0;
int g_cooldown = 0;

// Demandes venant de l'interface (traitees dans mod_update, sur le thread du jeu)
int g_reqWarp = -1;
int g_reqSetPortal = -1;
bool g_reqShowPos = false;
bool g_reqSavePos = false;
bool g_reqSpawn = false;
bool g_reqCancel = false;
bool g_reqScan = false;

int g_spawnMob = 0;      // monstre choisi dans le menu
int g_hardMobIdx = 0;    // monstre utilise par le mode difficile (copie de la config)

// Zone de portail / mode difficile / teinte
int g_zone = -1;
int g_lastRoom = -1;
int g_hardSpawnedThisRaid = 0;
bool g_tintActive = false;
bool g_wasInDungeon = false;

UiMenuTabHandle g_menuTab = 0;
const char* g_dataDir = nullptr;
int g_toastKey = 0;

// =============================================================================================
//  Utilitaires
// =============================================================================================

void toast(const char* title, const char* body, int ms = 3500) {
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = title;
    desc.body_rml = body;
    desc.duration_ms = ms;
    svc_ui->push_toast(mod_ctx, &desc);
}

bool cfgBool(ConfigVarHandle h) {
    bool b = false;
    svc_config->get_bool(mod_ctx, h, &b);
    return b;
}
int cfgInt(ConfigVarHandle h) {
    int64_t i = 0;
    svc_config->get_int(mod_ctx, h, &i);
    return static_cast<int>(i);
}
double cfgFloat(ConfigVarHandle h) {
    double d = 0;
    svc_config->get_float(mod_ctx, h, &d);
    return d;
}
std::string cfgString(ConfigVarHandle h) {
    char buf[64] = {0};
    if (svc_config->get_string(mod_ctx, h, buf, sizeof(buf), nullptr) != MOD_OK) {
        return std::string();
    }
    return std::string(buf);
}

bool regBool(const char* name, bool def, ConfigVarHandle* out) {
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = CONFIG_VAR_BOOL;
    d.default_bool = def;
    return svc_config->register_var(mod_ctx, &d, out) == MOD_OK;
}
bool regInt(const char* name, int def, ConfigVarHandle* out) {
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = CONFIG_VAR_INT;
    d.default_int = def;
    return svc_config->register_var(mod_ctx, &d, out) == MOD_OK;
}
bool regFloat(const char* name, double def, ConfigVarHandle* out) {
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = CONFIG_VAR_FLOAT;
    d.default_float = def;
    return svc_config->register_var(mod_ctx, &d, out) == MOD_OK;
}
bool regString(const char* name, const char* def, ConfigVarHandle* out) {
    ConfigVarDesc d = CONFIG_VAR_DESC_INIT;
    d.name = name;
    d.type = CONFIG_VAR_STRING;
    d.default_string = def;
    return svc_config->register_var(mod_ctx, &d, out) == MOD_OK;
}

float dist2D(const cXyz& p, float x, float z) {
    const float dx = p.x - x, dz = p.z - z;
    return std::sqrt(dx * dx + dz * dz);
}

std::string dungeonStage(int d) {
    const int i = std::clamp(cfgInt(v_dStage[d]), 0, kStageChoiceCount - 1);
    return kStageChoices[i];
}

bool insideDungeon(const char* stage, int d) {
    return std::strncmp(stage, dungeonStage(d).c_str(), 6) == 0;
}

bool isUnlocked(int d) {
    return cfgBool(v_unlockAll) || cfgBool(v_unlock[d]);
}

void teleportLocal(fopAc_ac_c* p, float x, float y, float z, s16 yaw) {
    p->current.pos.set(x, y, z);
    p->old.pos = p->current.pos;
    p->speed.set(0.0f, 0.0f, 0.0f);
    p->speedF = 0.0f;
    p->current.angle.y = yaw;
    p->shape_angle.y = yaw;
}

void loadPortal(int i) {
    Portal& p = g_portal[i];
    p.set = cfgBool(v_pSet[i]);
    p.stage = cfgString(v_pStage[i]);
    p.room = cfgInt(v_pRoom[i]);
    p.x = static_cast<float>(cfgFloat(v_pX[i]));
    p.y = static_cast<float>(cfgFloat(v_pY[i]));
    p.z = static_cast<float>(cfgFloat(v_pZ[i]));
}

// Ecrit une ligne dans locations.txt (dossier de donnees du mod).
bool appendLocation(const char* tag, const char* stage, int room, const cXyz& pos, s16 yaw) {
    if (g_dataDir == nullptr) {
        return false;
    }
    const std::string path = std::string(g_dataDir) + "/locations.txt";
    std::ofstream f(path, std::ios::app);
    if (!f) {
        return false;
    }
    char line[256];
    std::snprintf(line, sizeof(line), "%s | stage=%s room=%d x=%.1f y=%.1f z=%.1f yaw=%d", tag, stage,
        room, pos.x, pos.y, pos.z, static_cast<int>(yaw));
    f << line << "\n";
    mods::log::info("LOCATION {}", line);
    return true;
}

// Fait apparaitre un monstre devant Link (ou autour pour le mode difficile).
void spawnMob(fopAc_ac_c* player, int mobIdx, float distance, float angleOffsetRad, int room,
    float sizePct) {
    if (mobIdx < 0 || mobIdx >= kMobCount) {
        return;
    }
    const MobDef& m = kMobs[mobIdx];
    const float a = static_cast<float>(player->shape_angle.y) * (3.14159265f / 32768.0f) + angleOffsetRad;
    cXyz pos(player->current.pos.x + std::sin(a) * distance, player->current.pos.y + 20.0f,
        player->current.pos.z + std::cos(a) * distance);
    csXyz ang(0, static_cast<s16>(player->shape_angle.y + 0x8000), 0);
    const float s = (m.sizePct > 0 ? static_cast<float>(m.sizePct) : sizePct) / 100.0f;
    cXyz scale(s, s, s);
    mods::log::info("SPAWN {} (proc {}) x{:.2f} salle {}", m.label, m.proc, s, room);
    fopAcM_create(static_cast<s16>(m.proc), m.params, &pos, room, &ang, &scale, -1);
}

// =============================================================================================
//  Scanner de salle : liste tous les acteurs presents (nom, groupe, position) dans locations.txt
// =============================================================================================

struct ScanCtx {
    std::ofstream* file;
    int count;
    std::vector<std::pair<std::string, int>> tally;
};

const char* procLabel(int id) {
    for (int i = 0; i < kOrProcNameCount; ++i) {
        if (kOrProcNames[i].id == id) {
            return kOrProcNames[i].name;
        }
    }
    return "?";
}

int scanCb(void* actor, void* data) {
    ScanCtx* c = static_cast<ScanCtx*>(data);
    if (actor == nullptr || c->count >= 800) {
        return 1;
    }
    fopAc_ac_c* a = static_cast<fopAc_ac_c*>(actor);
    const int id = fopAcM_GetName(a);
    const char* name = procLabel(id);
    char line[200];
    std::snprintf(line, sizeof(line), "  ACTOR %s (id %d) groupe=%d x=%.0f y=%.0f z=%.0f", name, id,
        static_cast<int>(fopAcM_GetGroup(a)), a->current.pos.x, a->current.pos.y, a->current.pos.z);
    if (c->file != nullptr) {
        *c->file << line << "\n";
    }
    ++c->count;
    for (auto& t : c->tally) {
        if (t.first == name) {
            ++t.second;
            return 1;
        }
    }
    c->tally.push_back({name, 1});
    return 1;
}

void scanRoom(const char* stage, int room) {
    ScanCtx ctx{nullptr, 0, {}};
    std::ofstream f;
    if (g_dataDir != nullptr) {
        f.open(std::string(g_dataDir) + "/locations.txt", std::ios::app);
        if (f) {
            ctx.file = &f;
            f << "SCAN stage=" << stage << " room=" << room << "\n";
        }
    }
    fopAcIt_Executor(scanCb, &ctx);
    std::sort(ctx.tally.begin(), ctx.tally.end(),
        [](const auto& x, const auto& y) { return x.second > y.second; });
    std::string summary;
    for (const auto& t : ctx.tally) {
        summary += t.first + " x" + std::to_string(t.second) + "  ";
    }
    if (f) {
        f << "  RESUME " << summary << "\n";
    }
    mods::log::info("SCAN stage={} salle={} total={} : {}", stage, room, ctx.count, summary);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d acteurs. Voir locations.txt et le journal.", ctx.count);
    toast("Scan de la salle", buf, 4000);
}

// =============================================================================================
//  Teleportation vers un donjon + retour a l'origine
// =============================================================================================

void startWarp(fopAc_ac_c* player, const char* stage, int room, int d) {
    if (!isUnlocked(d)) {
        toast("Portail verrouillé", "Ce portail n'est pas encore débloqué.");
        return;
    }
    // On memorise l'endroit exact d'ou l'on part.
    g_origin.valid = true;
    g_origin.stage = stage;
    g_origin.room = room;
    g_origin.x = player->current.pos.x;
    g_origin.y = player->current.pos.y;
    g_origin.z = player->current.pos.z;
    g_origin.yaw = player->shape_angle.y;

    const int point = cfgInt(v_dPoint[d]);
    const int droom = cfgInt(v_dRoom[d]);
    const std::string dstage = dungeonStage(d);
    mods::log::info("WARP vers {} (point {}, salle {}) depuis {} salle {} ({:.0f},{:.0f},{:.0f})",
        dstage, point, droom, stage, room, g_origin.x, g_origin.y, g_origin.z);

    g_dun = d;
    g_phase = PH_GOING;
    g_phaseFrames = 0;
    g_hardSpawnedThisRaid = 0;
    g_lastRoom = -1;
    dComIfGp_setNextStage(dstage.c_str(), static_cast<s16>(point), static_cast<s8>(droom), -1);
    g_cooldown = 60;
}

void endRaid() {
    g_phase = PH_NONE;
    g_dun = -1;
    g_zone = -1;
    if (g_tintActive) {
        dKy_set_allcol_ratio(1.0f);
        dKy_set_fogcol_ratio(1.0f);
        dKy_set_vrboxcol_ratio(1.0f);
        g_tintActive = false;
    }
}

void updateRaid(fopAc_ac_c* player, const char* stage, int room) {
    ++g_phaseFrames;
    const bool inDun = (g_dun >= 0) && insideDungeon(stage, g_dun);
    const bool atOrigin = g_origin.valid && (g_origin.stage == stage);

    switch (g_phase) {
    case PH_GOING:
        if (inDun) {
            g_phase = PH_INSIDE;
            g_phaseFrames = 0;
            toast("Portail du Crépuscule", "Bonne chance...");
        } else if (g_phaseFrames > 900) {
            toast("Échec", "La téléportation n'a pas abouti.");
            endRaid();
        }
        break;

    case PH_INSIDE: {
        if (!inDun) {
            // On a quitte le donjon : retour la ou on etait.
            if (atOrigin) {
                g_phase = PH_ARRIVED;
            } else {
                g_phase = PH_RETURNING;
                mods::log::info("RETOUR vers {} salle {}", g_origin.stage, g_origin.room);
                dComIfGp_setNextStage(g_origin.stage.c_str(), 0, static_cast<s8>(g_origin.room), -1);
                g_cooldown = 60;
            }
            g_phaseFrames = 0;
            break;
        }

        // ---- teinte sombre "crepuscule" ----
        if (cfgBool(v_tint)) {
            const float r = static_cast<float>(cfgInt(v_tintPct)) / 100.0f;
            dKy_set_allcol_ratio(r);
            dKy_set_fogcol_ratio(r);
            dKy_set_vrboxcol_ratio(r * 0.8f);
            g_tintActive = true;
        } else if (g_tintActive) {
            dKy_set_allcol_ratio(1.0f);
            dKy_set_fogcol_ratio(1.0f);
            dKy_set_vrboxcol_ratio(1.0f);
            g_tintActive = false;
        }

        // ---- mode difficile : monstres en plus a chaque nouvelle salle ----
        if (cfgBool(v_hard) && room != g_lastRoom && g_phaseFrames > 120) {
            g_lastRoom = room;
            int n = cfgInt(v_extra);
            n = std::min(n, 6);
            if (g_hardSpawnedThisRaid + n <= 40) {  // plafond : la memoire du jeu est limitee
                const int idx = std::clamp(cfgInt(v_hardMob), 0, kMobCount - 1);
                for (int k = 0; k < n; ++k) {
                    const float ang = (6.2831853f / std::max(n, 1)) * static_cast<float>(k);
                    spawnMob(player, idx, 450.0f, ang, room, static_cast<float>(cfgInt(v_size)));
                }
                g_hardSpawnedThisRaid += n;
                char msg[96];
                std::snprintf(msg, sizeof(msg), "%d monstres supplémentaires surgissent !", n);
                toast("Mode difficile", msg, 2500);
            }
        } else if (room != g_lastRoom && g_phaseFrames <= 120) {
            g_lastRoom = room;
        }
        break;
    }

    case PH_RETURNING:
        if (atOrigin) {
            g_phase = PH_ARRIVED;
            g_phaseFrames = 0;
        } else if (g_phaseFrames > 900) {
            toast("Retour impossible", "Le retour automatique a échoué.");
            endRaid();
        }
        break;

    case PH_ARRIVED:
        if (g_phaseFrames > 45) {
            if (g_origin.valid) {
                teleportLocal(player, g_origin.x, g_origin.y + 10.0f, g_origin.z, g_origin.yaw);
            }
            toast("De retour", "Tu es revenu là où tu étais.");
            endRaid();
        }
        break;

    default:
        break;
    }
}

// =============================================================================================
//  Interface (menu F1) : les callbacks ne font que poser des demandes
// =============================================================================================

void cbWarp(ModContext*, void* ud) { g_reqWarp = static_cast<int>(reinterpret_cast<intptr_t>(ud)); }
void cbSetPortal(ModContext*, void* ud) { g_reqSetPortal = static_cast<int>(reinterpret_cast<intptr_t>(ud)); }
void cbShowPos(ModContext*, void*) { g_reqShowPos = true; }
void cbSavePos(ModContext*, void*) { g_reqSavePos = true; }
void cbSpawn(ModContext*, void*) { g_reqSpawn = true; }
void cbCancel(ModContext*, void*) { g_reqCancel = true; }
void cbScan(ModContext*, void*) { g_reqScan = true; }

void getStageSel(ModContext*, void* ud, UiControlValue* out) {
    out->int_value = std::clamp(cfgInt(v_dStage[reinterpret_cast<intptr_t>(ud)]), 0, kStageChoiceCount - 1);
}
void setStageSel(ModContext*, void* ud, const UiControlValue* v) {
    svc_config->set_int(mod_ctx, v_dStage[reinterpret_cast<intptr_t>(ud)], v->int_value);
}

void getSpawnMob(ModContext*, void*, UiControlValue* out) { out->int_value = g_spawnMob; }
void setSpawnMob(ModContext*, void*, const UiControlValue* v) { g_spawnMob = static_cast<int>(v->int_value); }

void getHardMob(ModContext*, void*, UiControlValue* out) { out->int_value = cfgInt(v_hardMob); }
void setHardMob(ModContext*, void*, const UiControlValue* v) {
    svc_config->set_int(mod_ctx, v_hardMob, v->int_value);
}

void addSection(UiElementHandle pane, const char* title) { svc_ui->pane_add_section(mod_ctx, pane, title); }

void addText(UiElementHandle pane, const char* text) {
    UiElementHandle h = 0;
    svc_ui->pane_add_text(mod_ctx, pane, text, &h);
}

void addButton(UiElementHandle pane, const char* label, UiPressedFn fn, intptr_t ud, const char* help) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_BUTTON;
    c.label = label;
    c.help_rml = help;
    c.on_pressed = fn;
    c.user_data = reinterpret_cast<void*>(ud);
    svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}

void addToggle(UiElementHandle pane, const char* label, ConfigVarHandle var, const char* help) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_TOGGLE;
    c.label = label;
    c.help_rml = help;
    c.binding = UI_BINDING_CONFIG_VAR;
    c.config_var = var;
    svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}

void addNumber(UiElementHandle pane, const char* label, ConfigVarHandle var, int mn, int mx, int step,
    const char* suffix, const char* help) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_NUMBER;
    c.label = label;
    c.help_rml = help;
    c.binding = UI_BINDING_CONFIG_VAR;
    c.config_var = var;
    c.min = mn;
    c.max = mx;
    c.step = step;
    c.suffix = suffix;
    svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}

std::vector<const char*>& mobOptions() {
    static std::vector<const char*> opts;
    if (opts.empty()) {
        for (int i = 0; i < kMobCount; ++i) {
            opts.push_back(kMobs[i].label);
        }
    }
    return opts;
}

void addMobDropdown(UiElementHandle pane, const char* label, UiControlGetFn get, UiControlSetFn set,
    const char* help) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_DROPDOWN;
    c.label = label;
    c.help_rml = help;
    c.binding = UI_BINDING_CALLBACKS;
    c.get = get;
    c.set = set;
    c.options = mobOptions().data();
    c.option_count = mobOptions().size();
    svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}

ModResult buildTeleport(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    addSection(left, "Téléporteurs");
    static const char* kBtn[3] = {"Aller au Temple de la Forêt (portail vert)",
        "Aller aux Mines des Gorons (portail rouge)", "Aller au Temple du Lac (portail bleu)"};
    for (int i = 0; i < 3; ++i) {
        addButton(left, kBtn[i], cbWarp, i,
            "Te téléporte dans le donjon. En sortant, tu reviens là où tu étais.");
    }
    addSection(left, "Destination de chaque portail");
    static const char* kDestLbl[3] = {"Portail vert : stage", "Portail rouge : stage", "Portail bleu : stage"};
    for (int i = 0; i < 3; ++i) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.kind = UI_CONTROL_DROPDOWN;
        c.label = kDestLbl[i];
        c.help_rml = "Si le portail t'envoie au mauvais endroit, essaie un autre stage de donjon.";
        c.binding = UI_BINDING_CALLBACKS;
        c.get = getStageSel;
        c.set = setStageSel;
        c.user_data = reinterpret_cast<void*>(static_cast<intptr_t>(i));
        c.options = kStageChoices;
        c.option_count = static_cast<size_t>(kStageChoiceCount);
        svc_ui->pane_add_control(mod_ctx, left, &c, nullptr);
    }
    addSection(left, "Déblocage");
    addToggle(left, "Tout débloquer", v_unlockAll, "Active les 3 portails.");
    addToggle(left, "Débloquer : Forêt (vert)", v_unlock[0], nullptr);
    addToggle(left, "Débloquer : Volcan (rouge)", v_unlock[1], nullptr);
    addToggle(left, "Débloquer : Lac (bleu)", v_unlock[2], nullptr);
    addText(right,
        "Les portails sont aussi dans le monde : approche-toi de leur emplacement (réglé dans l'onglet "
        "Débogage) et appuie sur A.");
    return MOD_OK;
}

ModResult buildDifficulty(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    addSection(left, "Mode difficile");
    addToggle(left, "Activer le mode difficile", v_hard,
        "Des monstres supplémentaires surgissent à chaque nouvelle salle du donjon.");
    addNumber(left, "Monstres en plus par salle", v_extra, 1, 6, 1, "", nullptr);
    addNumber(left, "Taille des monstres", v_size, 30, 250, 10, " %", "100 % = taille normale.");
    addMobDropdown(left, "Monstre du mode difficile", getHardMob, setHardMob,
        "Identifiant du monstre à faire apparaître (à choisir avec l'onglet Débogage).");
    addSection(left, "Ambiance");
    addToggle(left, "Teinte sombre Crépuscule", v_tint, "Assombrit l'ambiance dans le donjon.");
    addNumber(left, "Luminosité", v_tintPct, 20, 100, 5, " %", "Plus bas = plus sombre.");
    addText(right,
        "Mode difficile : plus de monstres (pas encore de boss modifiés). La teinte est une "
        "approximation : le vrai filtre doré nécessite un shader (prochaine étape).");
    return MOD_OK;
}

ModResult buildDebug(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
    ModError*) {
    addSection(left, "Position");
    addButton(left, "Afficher stage / salle / position", cbShowPos, 0, nullptr);
    addButton(left, "Enregistrer la position (fichier)", cbSavePos, 0,
        "Ajoute une ligne dans locations.txt (dossier de données du mod).");
    addButton(left, "Scanner la salle (liste des acteurs)", cbScan, 0,
        "Écrit tous les acteurs de la salle dans locations.txt et le journal.");
    addSection(left, "Emplacement des portails");
    static const char* kSet[3] = {"Placer le portail vert ici", "Placer le portail rouge ici",
        "Placer le portail bleu ici"};
    for (int i = 0; i < 3; ++i) {
        addButton(left, kSet[i], cbSetPortal, i, "Utilise la position actuelle de Link.");
    }
    addSection(left, "Point d'arrivée dans les donjons");
    static const char* kPt[3] = {"Forêt : point", "Volcan : point", "Lac : point"};
    static const char* kRm[3] = {"Forêt : salle", "Volcan : salle", "Lac : salle"};
    for (int i = 0; i < 3; ++i) {
        addNumber(left, kPt[i], v_dPoint[i], 0, 60, 1, "", nullptr);
        addNumber(left, kRm[i], v_dRoom[i], 0, 60, 1, "", nullptr);
    }
    addSection(left, "Monstres");
    addMobDropdown(left, "Monstre à faire apparaître", getSpawnMob, setSpawnMob,
        "Expérimental : certains peuvent faire planter. Note ceux qui marchent.");
    addNumber(left, "Taille", v_size, 30, 250, 10, " %", nullptr);
    addButton(left, "Faire apparaître devant Link", cbSpawn, 0, nullptr);
    addSection(left, "Sécurité");
    addButton(left, "Annuler le retour automatique", cbCancel, 0,
        "À utiliser si tu changes de sauvegarde pendant un donjon.");
    addText(right,
        "Dossier de données : fichier locations.txt. Envoie-moi son contenu (ou les lignes LOCATION "
        "et SPAWN du journal) pour que je règle les portails et les monstres.");
    return MOD_OK;
}

void openMenu(ModContext*, void*) {
    UiTabDesc tabs[3] = {UI_TAB_DESC_INIT, UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Téléporteurs";
    tabs[0].build = buildTeleport;
    tabs[1].title = "Difficulté";
    tabs[1].build = buildDifficulty;
    tabs[2].title = "Débogage";
    tabs[2].build = buildDebug;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 3;
    UiWindowHandle window = 0;
    svc_ui->window_push(mod_ctx, &desc, &window);
}

// =============================================================================================
//  Traitement des demandes + portails du monde
// =============================================================================================

void processRequests(fopAc_ac_c* player, const char* stage, int room) {
    if (g_reqCancel) {
        g_reqCancel = false;
        endRaid();
        toast("Retour annulé", "Le retour automatique est désactivé.");
    }
    if (g_reqShowPos) {
        g_reqShowPos = false;
        char buf[200];
        std::snprintf(buf, sizeof(buf), "stage %s, salle %d  |  x=%.0f y=%.0f z=%.0f", stage, room,
            player->current.pos.x, player->current.pos.y, player->current.pos.z);
        toast("Position actuelle", buf, 5000);
        mods::log::info("DIAG stage={} salle={} pos=({:.1f},{:.1f},{:.1f})", stage, room,
            player->current.pos.x, player->current.pos.y, player->current.pos.z);
    }
    if (g_reqScan) {
        g_reqScan = false;
        scanRoom(stage, room);
    }
    if (g_reqSavePos) {
        g_reqSavePos = false;
        const bool ok = appendLocation("POSITION", stage, room, player->current.pos, player->shape_angle.y);
        toast(ok ? "Position enregistrée" : "Échec de l'enregistrement",
            ok ? "Ajoutée à locations.txt (dossier de données du mod)." : "Dossier de données introuvable.");
    }
    if (g_reqSetPortal >= 0) {
        const int i = g_reqSetPortal;
        g_reqSetPortal = -1;
        svc_config->set_string(mod_ctx, v_pStage[i], stage);
        svc_config->set_int(mod_ctx, v_pRoom[i], room);
        svc_config->set_float(mod_ctx, v_pX[i], player->current.pos.x);
        svc_config->set_float(mod_ctx, v_pY[i], player->current.pos.y);
        svc_config->set_float(mod_ctx, v_pZ[i], player->current.pos.z);
        svc_config->set_bool(mod_ctx, v_pSet[i], true);
        loadPortal(i);
        char tag[48];
        std::snprintf(tag, sizeof(tag), "PORTAIL_%s", kDungeons[i].color);
        appendLocation(tag, stage, room, player->current.pos, player->shape_angle.y);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Portail %s placé ici (%s, salle %d).", kDungeons[i].color, stage,
            room);
        toast("Portail placé", buf);
    }
    if (g_reqSpawn) {
        g_reqSpawn = false;
        spawnMob(player, g_spawnMob, 300.0f, 0.0f, room, static_cast<float>(cfgInt(v_size)));
        toast("Monstre", kMobs[std::clamp(g_spawnMob, 0, kMobCount - 1)].label, 2000);
    }
    if (g_reqWarp >= 0 && g_phase == PH_NONE) {
        const int d = g_reqWarp;
        g_reqWarp = -1;
        startWarp(player, stage, room, d);
    } else if (g_reqWarp >= 0) {
        g_reqWarp = -1;
        toast("Déjà en cours", "Une téléportation est déjà en cours.");
    }
}

void updatePortals(fopAc_ac_c* player, const char* stage, int room, u32 trig) {
    int zone = -1;
    for (int i = 0; i < 3; ++i) {
        const Portal& p = g_portal[i];
        if (!p.set || p.stage != stage) {
            continue;
        }
        if (dist2D(player->current.pos, p.x, p.z) < 150.0f && std::fabs(player->current.pos.y - p.y) < 250.0f) {
            zone = i;
            break;
        }
    }
    if (zone >= 0 && zone != g_zone) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Portail %s : appuie sur A pour entrer (%s).",
            kDungeons[zone].color, kDungeons[zone].label);
        toast("Portail du Crépuscule", buf, 3000);
    }
    g_zone = zone;
    if (zone >= 0 && (trig & PAD_BUTTON_A)) {
        startWarp(player, stage, room, zone);
    }
}

}  // namespace

// =============================================================================================
//  Points d'entree du mod
// =============================================================================================

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError*) {
    bool ok = true;
    ok &= regBool("unlock_all", true, &v_unlockAll);
    ok &= regBool("hard_mode", false, &v_hard);
    ok &= regInt("extra_enemies", 2, &v_extra);
    ok &= regInt("mob_size", 100, &v_size);
    ok &= regBool("tint", false, &v_tint);
    ok &= regInt("tint_pct", 55, &v_tintPct);
    ok &= regInt("hard_mob", 0, &v_hardMob);

    static char names[3][8][24];
    for (int i = 0; i < 3; ++i) {
        std::snprintf(names[i][0], 24, "unlock_%d", i);
        std::snprintf(names[i][1], 24, "p%d_set", i);
        std::snprintf(names[i][2], 24, "p%d_stage", i);
        std::snprintf(names[i][3], 24, "p%d_room", i);
        std::snprintf(names[i][4], 24, "p%d_x", i);
        std::snprintf(names[i][5], 24, "p%d_y", i);
        std::snprintf(names[i][6], 24, "p%d_z", i);
        ok &= regBool(names[i][0], false, &v_unlock[i]);
        ok &= regBool(names[i][1], false, &v_pSet[i]);
        ok &= regString(names[i][2], "", &v_pStage[i]);
        ok &= regInt(names[i][3], 0, &v_pRoom[i]);
        ok &= regFloat(names[i][4], 0.0, &v_pX[i]);
        ok &= regFloat(names[i][5], 0.0, &v_pY[i]);
        ok &= regFloat(names[i][6], 0.0, &v_pZ[i]);

    }
    static char dnames[3][3][24];
    for (int i = 0; i < 3; ++i) {
        std::snprintf(dnames[i][0], 24, "d%d_point", i);
        std::snprintf(dnames[i][1], 24, "d%d_room", i);
        std::snprintf(dnames[i][2], 24, "d%d_stage", i);
        ok &= regInt(dnames[i][2], kDefStageIdx[i], &v_dStage[i]);
        ok &= regInt(dnames[i][0], kDungeons[i].defPoint, &v_dPoint[i]);
        ok &= regInt(dnames[i][1], kDungeons[i].defRoom, &v_dRoom[i]);
    }
    if (!ok) {
        mods::log::error("Impossible d'enregistrer toute la configuration du mod");
        return MOD_ERROR;
    }
    for (int i = 0; i < 3; ++i) {
        loadPortal(i);
    }

    if (svc_host->data_dir(mod_ctx, &g_dataDir) != MOD_OK) {
        g_dataDir = nullptr;
    }

    UiMenuTabDesc tab = UI_MENU_TAB_DESC_INIT;
    tab.label = "Crépuscule";
    tab.on_selected = openMenu;
    if (svc_ui->register_menu_tab(mod_ctx, &tab, &g_menuTab) != MOD_OK) {
        mods::log::error("Impossible d'ajouter l'onglet au menu");
    }

    mods::log::info("Donjons du Crepuscule v1.1 charge (dossier de donnees : {})",
        g_dataDir ? g_dataDir : "(inconnu)");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (g_cooldown > 0) {
        --g_cooldown;
        return MOD_OK;
    }

    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    const char* stage = dComIfGp_getStartStageName();
    if (player == nullptr || stage == nullptr || dComIfGp_event_runCheck()) {
        return MOD_OK;
    }
    const int room = dComIfGp_roomControl_getStayNo();
    const u32 trig = mDoCPd_c::getTrig(0);

    processRequests(player, stage, room);

    if (g_phase != PH_NONE) {
        updateRaid(player, stage, room);
    } else {
        updatePortals(player, stage, room, trig);
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    if (g_tintActive) {
        dKy_set_allcol_ratio(1.0f);
        dKy_set_fogcol_ratio(1.0f);
        dKy_set_vrboxcol_ratio(1.0f);
    }
    mods::log::info("Donjons du Crepuscule decharge");
    return MOD_OK;
}

}  // extern "C"
