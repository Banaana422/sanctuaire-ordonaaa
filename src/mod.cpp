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
#include <vector>

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

// ---- TP natif (secours) : warp du jeu vers un stage existant (Caverne des Epreuves) ---------
constexpr const char* kAltStage = "D_SB01";
constexpr int kAltPoint = 0;
constexpr int kAltRoom = 0;
constexpr int kAltLayer = -1;
constexpr int kBackPoint = 0;  // point d'arrivee dans la cave au retour (A VERIFIER)

// ---- Arene : tres loin et tres haut au-dessus de la cave -------------------------------------
constexpr float kArenaDx = 20000.0f;
constexpr float kArenaDy = 3000.0f;
constexpr float kArenaFallMargin = 1500.0f;  // sous ce niveau, on remet Link au depart

// ---- Dimensions du donjon (unites du jeu) ----------------------------------------------------
constexpr float kFloorT = 60.0f;   // epaisseur du sol
constexpr float kWallH = 500.0f;   // hauteur des murs
constexpr float kWallT = 60.0f;    // epaisseur des murs

// Points d'interaction (coordonnees locales a l'arene)
constexpr float kChestX = 0.0f, kChestZ = -300.0f;
constexpr float kGoalX = 0.0f, kGoalZ = 2600.0f;
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

struct BlockDef {
    int shape, kind;
    float cx, by, cz, sx, sy, sz;
};
std::vector<BlockDef> g_queue;
size_t g_next = 0;      // prochain bloc a creer
int g_pumpTimer = 0;

// cx/cz = centre, by = bas (coordonnees LOCALES), sx/sy/sz = taille voulue.
// Le bloc est mis en file d'attente : il est cree progressivement (voir pumpQueue).
void block(int shape, int kind, float cx, float by, float cz, float sx, float sy, float sz) {
    g_queue.push_back({shape, kind, cx, by, cz, sx, sy, sz});
}

void pumpQueue() {
    if (g_next >= g_queue.size()) {
        return;
    }
    if (++g_pumpTimer < 6) {
        return;
    }
    g_pumpTimer = 0;
    for (int n = 0; n < 2 && g_next < g_queue.size(); ++n, ++g_next) {
        const BlockDef& b = g_queue[g_next];
        cXyz pos(g_ax + b.cx, g_ay + b.by, g_az + b.cz);
        cXyz size(b.sx, b.sy, b.sz);
        csXyz ang(0, 0, 0);
        const auto id = fopAcM_create(maOrGhost_c::sProcName, OR_PARAM(b.kind, b.shape), &pos,
            g_room, &ang, &size, -1);
        mods::log::info("Bloc {}/{} demande (forme {}, type {}) id={}", g_next + 1, g_queue.size(),
            b.shape, b.kind, static_cast<unsigned>(id));
    }
}

bool dungeonReady() {
    return g_spawned && g_next >= g_queue.size() &&
           g_orBlocksReady >= static_cast<int>(g_queue.size());
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
    g_queue.clear();
    g_next = 0;
    g_pumpTimer = 0;
    g_ax = g_px + kArenaDx;
    g_ay = g_py + kArenaDy;
    g_az = g_pz;

    const int R = OR_GHOST_REAL;
    const int F = OR_GHOST_FALSE;
    const int H = OR_GHOST_HIDDEN;

    // Le donjon est un long couloir (axe z), 13 blocs seulement (la memoire du jeu est limitee).
    //
    //  z -600..600   SALLE 1 : coffre (masque). Mur avant = FAUX mur (disparait avec le masque).
    //  z  660..1160  SALLE 2 : entree
    //  z 1160..1560  GOUFFRE 1 : pierres visibles/solides seulement AVEC le masque
    //  z 1560..1900  ILOT central (sol normal) : on peut y changer de masque
    //  z 1900..2300  GOUFFRE 2 : pierres solides seulement SANS le masque
    //  z 2300..2800  SALLE FINALE : alcove de sortie
    //
    // Cote : x entre -600 et 600.

    // --- sols (le dessus est a y = 0) ---
    floorSlab(-600, 600, -600, 1160);    // salle 1 + debut salle 2
    floorSlab(-600, 600, 1560, 1900);    // ilot
    floorSlab(-600, 600, 2300, 2800);    // salle finale

    // --- pierres des gouffres ---
    hiddenStone(0, 1250, 200);           // gouffre 1 : AVEC le masque
    hiddenStone(0, 1470, 200);
    block(OR_SHAPE_SLAB, F, 0, -kFloorT, 1990, 200, kFloorT, 200);  // gouffre 2 : SANS le masque
    block(OR_SHAPE_SLAB, F, 0, -kFloorT, 2210, 200, kFloorT, 200);

    // --- murs ---
    wall(R, -660, -600, -660, 2860);     // mur gauche (toute la longueur)
    wall(R, 600, 660, -660, 2860);       // mur droit
    wall(R, -660, 660, -660, -600);      // fond de la salle 1
    wall(R, -660, 660, 2800, 2860);      // fond de la salle finale
    wall(F, -600, 600, 600, 660);        // FAUX mur entre les salles 1 et 2

    // --- "coffre" : socle de pierre ---
    block(OR_SHAPE_SLAB, R, kChestX, 0.0f, kChestZ, 140.0f, 90.0f, 140.0f);

    g_spawned = true;
    g_sinceSpawn = 0;
    mods::log::info("Donjon planifie : {} blocs, origine ({:.0f}, {:.0f}, {:.0f}), salle {}",
        g_queue.size(), g_ax, g_ay, g_az, room);
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

void orTrace(const char* what, int a, int b) {
    mods::log::info("[orghost] {} {} {}", what, a, b);
}

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
    mods::log::info("Sanctuaire d'Ordona v0.5 charge (miroir en {:.0f}, {:.0f}, {:.0f})", g_px,
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

    // ---- TP natif : retour depuis la Caverne des Epreuves avec L + R + Z -----------------------
    if (std::strcmp(stage, kAltStage) == 0) {
        if ((held & PAD_TRIGGER_L) && (held & PAD_TRIGGER_R) && (trig & PAD_TRIGGER_Z)) {
            mods::log::info("TP natif : retour vers {} salle {}", kPortalStage, kPortalRoom);
            dComIfGp_setNextStage(kPortalStage, kBackPoint, kPortalRoom, -1);
            g_cooldown = 120;
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

    // Le donjon ne se construit QUE quand on utilise le miroir (voir plus bas).
    ++g_roomFrames;
    if (g_spawned) {
        const int before = g_orBlocksReady;
        pumpQueue();
        if (g_orBlocksReady != before || dungeonReady()) {
            static int lastLogged = -1;
            if (g_orBlocksReady != lastLogged) {
                lastLogged = g_orBlocksReady;
                mods::log::info("Blocs prets : {}/{}", g_orBlocksReady, g_queue.size());
            }
        }
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
        toast("Le miroir brille...", "A : donjon.  Z + A : TP vers la Caverne des Epreuves.");
    }
    g_wasInZone = inZone;

    if (inZone && (trig & PAD_BUTTON_A) && (held & PAD_TRIGGER_Z)) {
        mods::log::info("TP natif vers {} point {} salle {}", kAltStage, kAltPoint, kAltRoom);
        dComIfGp_setNextStage(kAltStage, kAltPoint, kAltRoom, kAltLayer);
        g_cooldown = 120;
        return MOD_OK;
    }

    if (inZone && (trig & PAD_BUTTON_A)) {
        if (!g_spawned) {
            mods::log::info("Construction du donjon demandee");
            buildDungeon(room);
        }
        if (!dungeonReady()) {
            char buf[120];
            std::snprintf(buf, sizeof(buf), "Construction : %d/%d blocs. Reessaie dans un instant.",
                g_orBlocksReady, static_cast<int>(g_queue.size()));
            toast("Le miroir se prepare...", buf);
        } else {
            mods::log::info("Teleportation vers l'arene");
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
