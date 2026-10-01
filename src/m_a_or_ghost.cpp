// Bloc "fantome" : meme modele que le rocher de la demo officielle (archive Wrock).
// Selon le type et l'etat du masque, le bloc est visible/solide ou non.

#include "m_a_or_ghost.hpp"
#include "d/d_com_inf_game.h"
#include "res/Object/WRock.h"

static const char* l_resName = "Wrock";
static constexpr u32 heap_size = ALIGN_NEXT(13952, 0x20) + ALIGN_NEXT(1920, 0x20);

bool g_orMask = false;

maOrGhost_c::~maOrGhost_c() {
    if (mpCollider != NULL) {
        if (mRegistered) {
            dComIfG_Bgsp().Release(mpCollider);
            mRegistered = false;
        }
    }
    dComIfG_resDelete(&mPhase, l_resName);
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

cPhs_Step maOrGhost_c::create() {
    fopAcM_ct(this, maOrGhost_c);
    mRegistered = false;

    cPhs_Step step = dComIfG_resLoad(&mPhase, l_resName);
    if (step == cPhs_COMPLEATE_e) {
        if (!fopAcM_entrySolidHeap(this, createHeapCallBack, heap_size)) {
            return cPhs_ERROR_e;
        }

        fopAcM_SetMtx(this, mpModel->getBaseTRMtx());
        fopAcM_setCullSizeBox(this, -800.0f, -800.0f, -800.0f, 800.0f, 800.0f, 800.0f);

        mAcch.Set(&current.pos, &old.pos, this, 1, &mAcchCir, &speed, &current.angle, &shape_angle);

        Execute();
    }
    return step;
}

int maOrGhost_c::CreateHeap() {
    J3DModelData* model_data =
        (J3DModelData*)dComIfG_getObjectRes(l_resName, dRes_INDEX_WROCK_BMD_WROCK_e);
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
    cBgD_t* dzb = (cBgD_t*)dComIfG_getObjectRes(l_resName, dRes_INDEX_WROCK_DZB_WROCK_e);
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
    mAcch.CrrPos(dComIfG_Bgsp());

    mGndChk = mAcch.m_gnd;
    mGroundH = mAcch.GetGroundH();
    if (mGroundH != -G_CM3D_F_INF) {
        tevStr.YukaCol = dComIfG_Bgsp().GetPolyColor(mGndChk);
        tevStr.room_no = dComIfG_Bgsp().GetRoomId(mGndChk);
        fopAcM_SetRoomNo(this, dComIfG_Bgsp().GetRoomId(mGndChk));
    }

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
