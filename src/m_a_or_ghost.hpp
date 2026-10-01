#pragma once
#include "mods/svc/actor.h"
#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_s_acch.h"
#include "d/d_bg_w.h"

#define OR_GHOST_NAME "orghost"

// Type de bloc (parametre de l'acteur)
enum OrGhostKind {
    OR_GHOST_REAL = 2,    // vrai mur : toujours visible et solide
    OR_GHOST_FALSE = 0,   // faux mur : visible et solide... sauf avec le masque
    OR_GHOST_HIDDEN = 1,  // plateforme cachee : n'existe qu'avec le masque
};

// Etat du masque (defini dans mod.cpp)
extern bool g_orMask;

class maOrGhost_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    J3DModel* mpModel;
    dBgS_ObjAcch mAcch;
    cBgS_GndChk mGndChk;
    dBgS_AcchCir mAcchCir;
    Mtx mColliderMtx;
    dBgW* mpCollider;
    f32 mGroundH;
    bool mRegistered;

    virtual ~maOrGhost_c();
    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    bool isSolid() const;
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};
