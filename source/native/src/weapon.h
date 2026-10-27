#pragma once
#include "vrmath.h"

namespace weapon {
// Перехватывает отрисовку рук/оружия от первого лица.
bool Install();
// Рисовались ли руки игры в этом кадре (тогда модель правого контроллера не нужна).
bool TakeDrawn();
// В руке оружие ближнего боя (гаечный ключ): удар - взмахом руки.
bool MeleeEquipped();
bool RangedEquipped();
// Предмет в руке игрока (Pawn.ActiveHoldable) - каждый расчёт вида.
void NoteHoldable(void* held);
// Предмет в руке жёстко в правом контроллере: hands - актор рук, уже поставленный модом по
// контроллеру (handsLoc, handsRot). Вызывать каждый расчёт вида в игре.
void HoldRigid(void* held, void* hands, const vr::Vec3& handsLoc, const vr::Mat3& handsRot, vr::Vec3 gripPosition,const vr::Mat3* modelRotation=nullptr);
// Вышли из игрового вида (меню, ролик): предмет снова ставит движок.
void ReleaseRigid();
// Temporarily suppress the held actor, restoring only the bit owned by the mod.
void SetCinematic(bool active);
// Verified WP_WrenchMesh currently selected in the player's hand.
bool WrenchGeometrySelected();
bool PistolGeometrySelected();
// Named selected weapon and its verified upgrades/LODs only.
bool GeometryMesh(unsigned vertices,unsigned primitives);
void SetAbilityMode(bool active);
}
