// Sanctuaire d'Ordona - v0.4
//
// Donjon de deux salles, construit avec des dalles du Palais du Crepuscule, dans un grand
// espace vide au-dessus de la cave de Link. Pas de changement de stage : le miroir te
// teleporte. Salle 1 : un coffre donne le Masque de Zant. Salle 2 : une enigme qui l'utilise.
//
// Commandes :
//   A pres du miroir (cave)  : entrer dans le donjon
//   A pres du coffre         : ouvrir
//   R + Gauche               : mettre / retirer le masque (une fois obtenu)
//   L + R + Z                : revenir a la cave (secours)
//   R + Bas                  : affiche stage / salle / position (diagnostic)
//   R + Haut (dans la cave)  : replacer le miroir ici
//
// ATTENTION : non teste en jeu. Voir le README.

#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "mods/svc/ui.h"

#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor.h"
#include "m_Do/m_Do_controller_pad.h"

#include "m_a_or_ghost.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ActorService, svc_actor);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

namespace {

// ---- Cave de Link (releve en jeu) ------------------------------------------------------------
constexpr const char* kPortalStage = "R_SP01";
constexpr int kPortalRoom = 7;
constexpr float kPortalRadius = 120.0f;
constexpr float kDefaultX = 85.0f, kDefaultY = -1082.0f, kDefaultZ = -948.0f;

// ---- Arene : tres loin et tres haut au-dessus de la cave -------------------------------------
constexpr float kArenaDx = 20000.0f;
constexpr float kArenaDy = 6000.0f;
constexpr float kArenaFallMargin = 1500.0f;  // sous ce niveau, on remet Link au depart

// ---- Dimensions du donjon (unites du jeu) ----------------------------------------------------
constexpr float kFloorT = 60.0f;   // epaisseur du sol
constexpr float kWallH = 500.0f;   // hauteur des murs
constexpr float kWallT = 60.0f;    // epaisseur des murs

// Points d'interaction (coordonnees locales a l'arene)
constexpr float kChestX = 0.0f, kChestZ = -300.0f;
constexpr float kGoalX = 0.0f, kGoalZ = 3250.0f;
constexpr float kUseRadius = 220.0f;

ConfigVarHandle g_varSet{};
ConfigVarHandle g_varX{};
ConfigVarHandle g_varY{};
ConfigVarHandle g_varZ{};

bool g_portalSet = false;
float g_px = kDefaultX, g_py = kDefaultY, g_pz = kDefaultZ;

bool g_wasInZone = false;
bool g_hasMask = false;       // masque obtenu
bool g_inArena = false;
bool g_spawned = false;
int g_roomFrames = 0;         // frames passees dans la cave
int g_sinceSpawn = 0;         // frames depuis la creation du donjon
int g_cooldown = 0;
float g_ax = 0, g_ay = 0, g_az = 0;  // origine de l'arene (monde)

void toast(const char* title, const char* body) {
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = title;
    desc.body_rml = body;
    desc.duration_ms = 4000;
    svc_ui->push_toast(mod_ctx, &desc);
}

void loadConfig() {
    bool set = false;
    double x = 0, y = 0, z = 0;
    svc_config->get_bool(mod_ctx, g_varSet, &set);
    svc_config->get_float(mod_ctx, g_varX, &x);
    svc_config->get_float(mod_ctx, g_varY, &y);
    svc_config->get_float(mod_ctx, g_varZ, &z);
    if (set) {
        g_portalSet = true;
        g_px = static_cast<float>(x);
        g_py = static_cast<float>(y);
        g_pz = static_cast<float>(z);
    }
}

void savePortal(float x, float y, float z) {
    svc_config->set_float(mod_ctx, g_varX, x);
    svc_config->set_float(mod_ctx, g_varY, y);
    svc_config->set_float(mod_ctx, g_varZ, z);
    svc_config->set_bool(mod_ctx, g_varSet, true);
    g_px = x;
    g_py = y;
    g_pz = z;
    g_portalSet = true;
}

bool registerVar(const char* name, ConfigVarType type, ConfigVarHandle* out) {
    ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
    desc.name = name;
    desc.type = type;
    return svc_config->register_var(mod_ctx, &desc, out) == MOD_OK;
}

void teleport(fopAc_ac_c* p, float x, float y, float z, s16 yaw) {
    p->current.pos.set(x, y, z);
    p->old.pos = p->current.pos;
    p->speed.set(0.0f, 0.0f, 0.0f);
    p->speedF = 0.0f;
    p->current.angle.y = yaw;
    p->shape_angle.y = yaw;
}

// ---- Construction du donjon -------------------------------------------------------------------
int g_room = 0;

// cx/cz = centre, by = bas (coordonnees LOCALES), sx/sy/sz = taille voulue.
void block(int shape, int kind, float cx, float by, float cz, float sx, float sy, float sz) {
    cXyz pos(g_ax + cx, g_ay + by, g_az + cz);
    cXyz size(sx, sy, sz);
    csXyz ang(0, 0, 0);
    const auto id = fopAcM_create(maOrGhost_c::sProcName, OR_PARAM(kind, shape), &pos, g_room, &ang,
        &size, -1);
    if (id == fpcM_ERROR_PROCESS_ID_e) {
        mods::log::error("Creation du bloc impossible (shape {} kind {})", shape, kind);
    }
}

// Sol : de x0 a x1, de z0 a z1 ; le dessus est a y = 0 local.
void floorSlab(float x0, float x1, float z0, float z1) {
    block(OR_SHAPE_SLAB, OR_GHOST_REAL, 0.5f * (x0 + x1), -kFloorT, 0.5f * (z0 + z1), x1 - x0,
        kFloorT, z1 - z0);
}

// Mur : de x0 a x1, de z0 a z1 ; du sol vers le haut.
void wall(int kind, float x0, float x1, float z0, float z1) {
    block(OR_SHAPE_SLAB, kind, 0.5f * (x0 + x1), 0.0f, 0.5f * (z0 + z1), x1 - x0, kWallH, z1 - z0);
}

// Pierre cachee (visible et solide seulement avec le masque).
void hiddenStone(float cx, float cz, float size) {
    block(OR_SHAPE_SLAB, OR_GHOST_HIDDEN, cx, -kFloorT, cz, size, kFloorT, size);
}

void buildDungeon(int room) {
    g_room = room;
    g_ax = g_px + kArenaDx;
    g_ay = g_py + kArenaDy;
    g_az = g_pz;

    const int R = OR_GHOST_REAL;
    const int F = OR_GHOST_FALSE;

    // ===== SALLE 1 : x [-600, 600], z [-600, 600] =====
    floorSlab(-600, 600, -600, 600);
    wall(R, -660, 660, -660, -600);   // mur du fond
    wall(R, -660, -600, -600, 600);   // mur gauche
    wall(R, 600, 660, -600, 600);     // mur droit
    wall(R, -660, -160, 600, 660);    // mur avant, a gauche de la porte
    wall(R, 160, 660, 600, 660);      // mur avant, a droite de la porte
    wall(F, -160, 160, 600, 660);     // FAUSSE porte : disparait avec le masque

    // coffre (modele du jeu)
    block(OR_SHAPE_CHEST, R, kChestX, 0.0f, kChestZ, 1.4f, 1.4f, 1.4f);

    // ===== COULOIR : x [-160, 160], z [600, 1200] =====
    floorSlab(-160, 160, 600, 1200);
    wall(R, -220, -160, 660, 1200);
    wall(R, 160, 220, 660, 1200);

    // ===== SALLE 2 : x [-700, 700], z [1200, 3000], un gouffre entre z=1700 et z=2100 =====
    floorSlab(-700, 700, 1200, 1700);
    floorSlab(-700, 700, 2100, 3000);
    hiddenStone(0, 1800, 180);        // pierres visibles seulement avec le masque
    hiddenStone(0, 2000, 180);
    wall(R, -760, -700, 1200, 3060);  // mur gauche
    wall(R, 700, 760, 1200, 3060);    // mur droit
    wall(R, -760, -160, 1200, 1260);  // mur avant, a gauche du couloir
    wall(R, 160, 760, 1200, 1260);    // mur avant, a droite du couloir
    wall(R, -760, -120, 3000, 3060);  // mur du fond, a gauche
    wall(R, 120, 760, 3000, 3060);    // mur du fond, a droite
    wall(F, -120, 120, 3000, 3060);   // FAUSSE porte du fond : disparait avec le masque

    // ===== ALCOVE FINALE : x [-120, 120], z [3000, 3400] =====
    floorSlab(-180, 180, 3000, 3460);
    wall(R, -180, -120, 3000, 3460);
    wall(R, 120, 180, 3000, 3460);
    wall(R, -180, 180, 3400, 3460);

    g_spawned = true;
    g_sinceSpawn = 0;
    mods::log::info("Donjon cree : origine arene ({:.0f}, {:.0f}, {:.0f}), salle {}", g_ax, g_ay,
        g_az, room);
}

void enterArena(fopAc_ac_c* player) {
    teleport(player, g_ax, g_ay + 50.0f, g_az - 450.0f, 0);
    g_inArena = true;
    g_cooldown = 30;
    toast("Le miroir t'avale...", "Fouille la salle. Un coffre brille au centre.");
}

void leaveArena(fopAc_ac_c* player, const char* title, const char* body) {
    teleport(player, g_px, g_py + 20.0f, g_pz + 150.0f, 0);
    g_inArena = false;
    g_wasInZone = true;  // evite de re-declencher le message tout de suite
    g_cooldown = 60;
    toast(title, body);
}

float dist2D(const cXyz& p, float x, float z) {
    const float dx = p.x - x, dz = p.z - z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError*) {
    if (!registerVar("portal_set", CONFIG_VAR_BOOL, &g_varSet) ||
        !registerVar("portal_x", CONFIG_VAR_FLOAT, &g_varX) ||
        !registerVar("portal_y", CONFIG_VAR_FLOAT, &g_varY) ||
        !registerVar("portal_z", CONFIG_VAR_FLOAT, &g_varZ))
    {
        mods::log::error("Impossible d'enregistrer la config du mod");
        return MOD_ERROR;
    }
    if (svc_actor->register_actor(mod_ctx, &maOrGhost_c::sProfile, &maOrGhost_c::sProcName,
            &maOrGhost_c::sActorHandle) != MOD_OK)
    {
        mods::log::error("Impossible d'enregistrer l'acteur " OR_GHOST_NAME);
        return MOD_ERROR;
    }
    loadConfig();
    mods::log::info("Sanctuaire d'Ordona v0.4 charge (miroir en {:.0f}, {:.0f}, {:.0f})", g_px,
        g_py, g_pz);
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (g_cooldown > 0) {
        --g_cooldown;
        return MOD_OK;
    }

    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    if (player == nullptr || dComIfGp_event_runCheck()) {
        return MOD_OK;
    }
    const char* stage = dComIfGp_getStartStageName();
    if (stage == nullptr) {
        return MOD_OK;
    }
    const int room = dComIfGp_roomControl_getStayNo();
    const u32 held = mDoCPd_c::getHold(0);
    const u32 trig = mDoCPd_c::getTrig(0);
    const cXyz& pos = player->current.pos;

    // ---- Diagnostic : R + Bas -----------------------------------------------------------------
    if ((held & PAD_TRIGGER_R) && (trig & PAD_BUTTON_DOWN)) {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "stage %s, salle %d  |  x=%.0f y=%.0f z=%.0f", stage, room,
            pos.x, pos.y, pos.z);
        toast("Position actuelle", buf);
        mods::log::info("DIAG stage={} salle={} pos=({:.1f},{:.1f},{:.1f})", stage, room, pos.x,
            pos.y, pos.z);
        return MOD_OK;
    }

    // ---- Masque de Zant : R + Gauche ----------------------------------------------------------
    if ((held & PAD_TRIGGER_R) && (trig & PAD_BUTTON_LEFT)) {
        if (!g_hasMask) {
            toast("Pas de masque", "Tu ne possedes pas encore le masque.");
        } else {
            g_orMask = !g_orMask;
            toast(g_orMask ? "Masque equipe" : "Masque retire",
                g_orMask ? "Les faux murs disparaissent, les passages caches apparaissent."
                         : "Retour a la vue normale.");
        }
        return MOD_OK;
    }

    // ---- Si on quitte la cave (autre stage), tout est reinitialise ----------------------------
    if (std::strcmp(stage, kPortalStage) != 0) {
        g_inArena = false;
        g_spawned = false;
        g_roomFrames = 0;
        g_wasInZone = false;
        return MOD_OK;
    }

    // ============================== DANS LE DONJON =============================================
    if (g_inArena) {
        // Retour de secours : L + R + Z
        if ((held & PAD_TRIGGER_L) && (held & PAD_TRIGGER_R) && (trig & PAD_TRIGGER_Z)) {
            leaveArena(player, "Retour", "Tu es revenu dans la cave.");
            return MOD_OK;
        }
        // Chute : on remet Link au depart
        if (pos.y < g_ay - kArenaFallMargin) {
            teleport(player, g_ax, g_ay + 50.0f, g_az - 450.0f, 0);
            toast("Tu es tombe", "Retour au debut du donjon.");
            return MOD_OK;
        }
        // Coffre
        if (!g_hasMask && dist2D(pos, g_ax + kChestX, g_az + kChestZ) < kUseRadius &&
            (trig & PAD_BUTTON_A))
        {
            g_hasMask = true;
            toast("Masque de Zant obtenu !", "R + Gauche : mettre ou retirer le masque.");
            return MOD_OK;
        }
        // Sortie du donjon
        if (dist2D(pos, g_ax + kGoalX, g_az + kGoalZ) < kUseRadius && (trig & PAD_BUTTON_A)) {
            leaveArena(player, "Donjon termine !", "Bravo, tu as resolu l'enigme.");
            return MOD_OK;
        }
        return MOD_OK;
    }

    // ============================== DANS LA CAVE ===============================================
    if (room != kPortalRoom) {
        g_roomFrames = 0;
        g_spawned = false;
        g_wasInZone = false;
        return MOD_OK;
    }

    // Le donjon se construit ~1 s apres l'arrivee dans la cave.
    ++g_roomFrames;
    if (!g_spawned && g_roomFrames > 60) {
        buildDungeon(room);
    }
    if (g_spawned) {
        ++g_sinceSpawn;
    }

    // Calibration du miroir : R + Haut
    if ((held & PAD_TRIGGER_R) && (trig & PAD_BUTTON_UP)) {
        savePortal(pos.x, pos.y, pos.z);
        toast("Miroir place", "Position enregistree (le donjon sera reconstruit a la prochaine visite).");
        return MOD_OK;
    }

    const float d = dist2D(pos, g_px, g_pz);
    const bool inZone = d < kPortalRadius && std::fabs(pos.y - g_py) < 200.0f;
    if (inZone && !g_wasInZone) {
        toast("Le miroir brille...", "Appuie sur A pour entrer.");
    }
    g_wasInZone = inZone;

    if (inZone && (trig & PAD_BUTTON_A)) {
        if (!g_spawned || g_sinceSpawn < 240) {
            toast("Le miroir se prepare...", "Reessaie dans quelques secondes.");
        } else {
            enterArena(player);
        }
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    mods::log::info("Sanctuaire d'Ordona decharge");
    return MOD_OK;
}

}  // extern "C"
