// Sanctuaire d'Ordona - prototype v0.3
//
// Ce que fait ce mod :
//  1. Dans la maison de Link (stage R_SP01, salle 4) il surveille la position de Link.
//  2. Si Link est dans la zone du "miroir", un message s'affiche ; appuyer sur A
//     lance le changement de stage vers le donjon.
//  3. Dans le donjon, L + R + Z ensemble ramenent Link dans la cave.
//
// La position du miroir se regle EN JEU : tiens R et appuie sur Haut (croix directionnelle)
// a l'endroit voulu dans la cave. La position est sauvegardee dans la config du mod.
//
// ATTENTION : non teste en jeu (voir README). Les valeurs marquees "A VERIFIER" sont des
// hypotheses a confirmer avec les messages du journal (console Dusklight).

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"
#include "mods/svc/ui.h"

#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor.h"
#include "m_Do/m_Do_controller_pad.h"

#include <cmath>
#include <cstring>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

namespace {

// ---- Reglages (A VERIFIER dans le journal du jeu) -------------------------------------------
// Cave de Link : stage + salle ou se trouve le miroir.
constexpr const char* kPortalStage = "R_SP01";
constexpr int kPortalRoom = 4;
constexpr float kPortalRadius = 120.0f;  // rayon d'activation (unites du jeu)

// Destination : on reutilise la Caverne des Epreuves (D_SB01) comme squelette de donjon.
// Un vrai nouveau donjon (nouvelle geometrie) ne peut pas etre cree par un mod seul.
constexpr const char* kDestStage = "D_SB01";
constexpr int kDestPoint = 0;   // point d'entree  (A VERIFIER)
constexpr int kDestRoom = 0;    // salle de depart (A VERIFIER)
constexpr int kDestLayer = -1;  // -1 = calque par defaut

// Retour vers la cave de Link (A VERIFIER : le numero de point d'entree).
constexpr int kReturnPoint = 0;
constexpr int kReturnRoom = kPortalRoom;
constexpr int kReturnLayer = -1;
// ----------------------------------------------------------------------------------------------

ConfigVarHandle g_varSet{};
ConfigVarHandle g_varX{};
ConfigVarHandle g_varY{};
ConfigVarHandle g_varZ{};

bool g_portalSet = false;
float g_px = 0.0f, g_py = 0.0f, g_pz = 0.0f;
bool g_wasInZone = false;
bool g_inSanctuary = false;
int g_cooldown = 0;  // frames d'attente apres un warp

void toast(const char* title, const char* body) {
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = title;
    desc.body_rml = body;
    desc.duration_ms = 3500;
    svc_ui->push_toast(mod_ctx, &desc);
}

void loadConfig() {
    bool set = false;
    double x = 0, y = 0, z = 0;
    svc_config->get_bool(mod_ctx, g_varSet, &set);
    svc_config->get_float(mod_ctx, g_varX, &x);
    svc_config->get_float(mod_ctx, g_varY, &y);
    svc_config->get_float(mod_ctx, g_varZ, &z);
    g_portalSet = set;
    g_px = static_cast<float>(x);
    g_py = static_cast<float>(y);
    g_pz = static_cast<float>(z);
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
    loadConfig();
    mods::log::info("Sanctuaire d'Ordona charge. Miroir {}.",
        g_portalSet ? "deja calibre" : "NON calibre (R + Haut dans la cave)");
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

    // ---- Dans le donjon : retour avec L + R + Z -------------------------------------------
    if (std::strcmp(stage, kDestStage) == 0) {
        if (g_inSanctuary && (held & PAD_TRIGGER_L) && (held & PAD_TRIGGER_R) &&
            (trig & PAD_TRIGGER_Z))
        {
            mods::log::info("Retour vers {} (point {}, salle {})", kPortalStage, kReturnPoint,
                kReturnRoom);
            dComIfGp_setNextStage(kPortalStage, kReturnPoint, kReturnRoom, kReturnLayer);
            g_inSanctuary = false;
            g_cooldown = 120;
        }
        return MOD_OK;
    }
    g_inSanctuary = false;

    // ---- Dans la cave de Link --------------------------------------------------------------
    if (std::strcmp(stage, kPortalStage) != 0 || room != kPortalRoom) {
        g_wasInZone = false;
        return MOD_OK;
    }

    const cXyz& pos = player->current.pos;

    // Calibration : R maintenu + Haut sur la croix directionnelle.
    if ((held & PAD_TRIGGER_R) && (trig & PAD_BUTTON_UP)) {
        savePortal(pos.x, pos.y, pos.z);
        mods::log::info("Miroir place en ({:.1f}, {:.1f}, {:.1f}) dans {} salle {}", pos.x, pos.y,
            pos.z, stage, room);
        toast("Miroir place", "Position enregistree. Reviens près du miroir et appuie sur A.");
        return MOD_OK;
    }

    if (!g_portalSet) {
        return MOD_OK;
    }

    const float dx = pos.x - g_px;
    const float dz = pos.z - g_pz;
    const float dy = pos.y - g_py;
    const bool inZone = (dx * dx + dz * dz) < (kPortalRadius * kPortalRadius) &&
                        std::fabs(dy) < 200.0f;

    if (inZone && !g_wasInZone) {
        toast("Le miroir brille...", "Appuie sur A pour entrer.");
    }
    g_wasInZone = inZone;

    if (inZone && (trig & PAD_BUTTON_A)) {
        mods::log::info("Entree dans le sanctuaire : {} point {} salle {}", kDestStage,
            kDestPoint, kDestRoom);
        dComIfGp_setNextStage(kDestStage, kDestPoint, kDestRoom, kDestLayer);
        g_inSanctuary = true;
        g_cooldown = 120;
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    mods::log::info("Sanctuaire d'Ordona decharge");
    return MOD_OK;
}

}  // extern "C"
