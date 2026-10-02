// Bloc "fantome" generique : une forme (modele du jeu) + un comportement lie au masque.
// Pour les dalles (OR_SHAPE_SLAB), l'acteur MESURE le modele et l'etire pour obtenir exactement
// la taille demandee (scale = taille voulue en unites du jeu). Position = centre en X/Z, bas en Y.

#include "m_a_or_ghost.hpp"
#include "d/d_com_inf_game.h"
#include "res/Object/WRock.h"
#include "res/Object/L8Lift.h"
#include "res/Object/Tbox2.h"

namespace {
struct ShapeDef {
    const char* arc;
    int bmd;
    int dzb;
    u32 heap;
    bool autoSize;
};
const ShapeDef kShapes[OR_SHAPE_COUNT] = {
    {"Wrock", dRes_INDEX_WROCK_BMD_WROCK_e, dRes_INDEX_WROCK_DZB_WROCK_e, 0x6000, false},
    {"L8Lift", dRes_INDEX_L8LIFT_BMD_LV8_LIFTX_e, dRes_INDEX_L8LIFT_DZB_LV8_LIFTX_e, 0x8000, true},
    {"Tbox2", dRes_INDEX_TBOX2_BMD_BOXA_e, dRes_INDEX_TBOX2_DZB_BOXAC_e, 0x8000, false},
};
}  // namespace

bool g_orMask = false;

maOrGhost_c::~maOrGhost_c() {
    if (mpCollider != NULL && mRegistered) {
        dComIfG_Bgsp().Release(mpCollider);
        mRegistered = false;
    }
    if (mArc != NULL) {
        dComIfG_resDelete(&mPhase, mArc);
    }
}

bool maOrGhost_c::isSolid() const {
    switch (parameters & 0xFF) {
    case OR_GHOST_FALSE:
        return !g_orMask;
    case OR_GHOST_HIDDEN:
        return g_orMask;
    default:
        return true;
    }
}

void maOrGhost_c::fitToTarget() {
    const ShapeDef& sd = kShapes[mShape];
    const cXyz target = scale;

    // 1) mesure du modele a l'echelle 1
    scale.set(1.0f, 1.0f, 1.0f);
    fopAcM_setCullSizeBox2(this, mpModel->getModelData());
    const f32 minX = cull.box.min.x, minY = cull.box.min.y, minZ = cull.box.min.z;
    const f32 maxX = cull.box.max.x, maxY = cull.box.max.y, maxZ = cull.box.max.z;

    // 2) echelle finale
    if (sd.autoSize) {
        f32 bw = maxX - minX, bh = maxY - minY, bd = maxZ - minZ;
        if (bw < 1.0f) bw = 1.0f;
        if (bh < 1.0f) bh = 1.0f;
        if (bd < 1.0f) bd = 1.0f;
        scale.set(target.x / bw, target.y / bh, target.z / bd);
    } else if (target.x > 0.0f && target.y > 0.0f && target.z > 0.0f) {
        scale = target;
    }

    // 3) ancrage : centre en X/Z, bas en Y
    current.pos.x -= 0.5f * (minX + maxX) * scale.x;
    current.pos.z -= 0.5f * (minZ + maxZ) * scale.z;
    current.pos.y -= minY * scale.y;
    old.pos = current.pos;

    fopAcM_setCullSizeBox2(this, mpModel->getModelData());
}

cPhs_Step maOrGhost_c::create() {
    fopAcM_ct(this, maOrGhost_c);
    mRegistered = false;
    mArc = NULL;
    mpCollider = NULL;
    mpModel = NULL;

    const int shapeId = static_cast<int>((parameters >> 8) & 0xFF);
    mShape = (shapeId < OR_SHAPE_COUNT) ? shapeId : 0;
    const ShapeDef& sd = kShapes[mShape];
    mArc = sd.arc;

    cPhs_Step step = dComIfG_resLoad(&mPhase, sd.arc);
    if (step == cPhs_COMPLEATE_e) {
        if (!fopAcM_entrySolidHeap(this, createHeapCallBack, sd.heap)) {
            return cPhs_ERROR_e;
        }
        fopAcM_SetMtx(this, mpModel->getBaseTRMtx());
        fitToTarget();
        Execute();
    }
    return step;
}

int maOrGhost_c::CreateHeap() {
    const ShapeDef& sd = kShapes[mShape];
    J3DModelData* model_data = (J3DModelData*)dComIfG_getObjectRes(sd.arc, sd.bmd);
    if (model_data == NULL) {
        return 0;
    }
    mpModel = mDoExt_J3DModel__create(model_data, 0x80000, 0x11000084);
    if (mpModel == NULL) {
        return 0;
    }

    mpCollider = JKR_NEW dBgW();
    if (mpCollider == NULL) {
        return 0;
    }
    cBgD_t* dzb = (cBgD_t*)dComIfG_getObjectRes(sd.arc, sd.dzb);
    if (mpCollider->Set(dzb, 1, &mColliderMtx) == true) {
        return 0;
    }
    mpCollider->SetCrrFunc(dBgS_MoveBGProc_Typical);
    return 1;
}

int maOrGhost_c::createHeapCallBack(fopAc_ac_c* i_this) {
    return static_cast<maOrGhost_c*>(i_this)->CreateHeap();
}

int maOrGhost_c::Delete() {
    this->~maOrGhost_c();
    return 1;
}

int maOrGhost_c::Execute() {
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::ZXYrotM(shape_angle);
    mDoMtx_stack_c::scaleM(scale);
    mpModel->setBaseTRMtx(mDoMtx_stack_c::get());

    // Collision : active seulement quand le bloc est "solide" selon l'etat du masque.
    if (mpCollider != NULL) {
        const bool solid = isSolid();
        if (solid && !mRegistered) {
            if (dComIfG_Bgsp().Regist(mpCollider, this) != true) {
                mRegistered = true;
            }
        } else if (!solid && mRegistered) {
            dComIfG_Bgsp().Release(mpCollider);
            mRegistered = false;
        }
        if (mRegistered) {
            PSMTXCopy(mpModel->getBaseTRMtx(), mColliderMtx);
            mpCollider->Move();
        }
    }

    eyePos = attention_info.position = current.pos;
    attention_info.flags = 0;
    return 1;
}

int maOrGhost_c::Draw() {
    if (!isSolid()) {
        return 1;  // invisible
    }
    g_env_light.settingTevStruct(0x20, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(mpModel, &tevStr);
    dComIfGd_setListBG();
    mDoExt_modelUpdateDL(mpModel);
    dComIfGd_setList();
    return 1;
}

static cPhs_Step maOrGhost_Create(void* i_this) {
    return static_cast<maOrGhost_c*>(i_this)->create();
}
static int maOrGhost_Delete(void* i_this) {
    return static_cast<maOrGhost_c*>(i_this)->Delete();
}
static int maOrGhost_Execute(void* i_this) {
    return static_cast<maOrGhost_c*>(i_this)->Execute();
}
static int maOrGhost_Draw(void* i_this) {
    return static_cast<maOrGhost_c*>(i_this)->Draw();
}
static int maOrGhost_IsDelete(void*) {
    return 1;
}

s16 maOrGhost_c::sProcName = -1;
ActorHandle maOrGhost_c::sActorHandle = -1;
const ActorProfileDesc maOrGhost_c::sProfile = {.name = OR_GHOST_NAME,
    .priority_group = 7,
    .process_size = sizeof(maOrGhost_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = maOrGhost_Create,
    .delete_function = maOrGhost_Delete,
    .execute_function = maOrGhost_Execute,
    .is_delete_function = maOrGhost_IsDelete,
    .draw_function = maOrGhost_Draw};
