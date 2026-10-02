#pragma once

// IMPORTANT : ces en-tetes standard doivent etre inclus AVANT ceux du jeu, sinon les macros du
// jeu perturbent la STL de Visual Studio (erreur C2440 dans <utility>).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "mods/svc/actor.h"
#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_s_acch.h"
#include "d/d_bg_w.h"

#define OR_GHOST_NAME "orghost"

// Forme (modele du jeu) : bits 8-15 du parametre. Comportement : bits 0-7.
enum OrShape {
    OR_SHAPE_ROCK = 0,   // rocher (archive Wrock)
    OR_SHAPE_SLAB = 1,   // dalle du Palais du Crepuscule (archive L8Lift) : sol, murs
    OR_SHAPE_CHEST = 2,  // coffre (archive Tbox2)
    OR_SHAPE_COUNT
};
#define OR_PARAM(kind, shape) (static_cast<u32>((kind) | ((shape) << 8)))

// Comportement du bloc (parametre de l'acteur)
enum OrGhostKind {
    OR_GHOST_REAL = 2,    // vrai mur : toujours visible et solide
    OR_GHOST_FALSE = 0,   // faux mur : visible et solide... sauf avec le masque
    OR_GHOST_HIDDEN = 1,  // plateforme cachee : n'existe qu'avec le masque
    OR_GHOST_CHEST = 3,   // coffre (modele du jeu, archive Dalways)
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
    const char* mArc;
    int mShape;

    virtual ~maOrGhost_c();
    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    bool isSolid() const;
    void fitToTarget();
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};
