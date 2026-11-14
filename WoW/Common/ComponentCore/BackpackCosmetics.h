#pragma once
#include <cstdint>
#include <string>
#include "Engine/Source/Retail/BackpackFit.h"
class CCharacterComponent;
class CM2Model;
void EsteriaBackpackEquipment(CCharacterComponent* character);
bool EsteriaBackpackPending(CCharacterComponent* character);
void EsteriaBackpackForget(CCharacterComponent* character);
bool EsteriaBackpackPreview(CM2Model* model, uint32_t display);
bool EsteriaBackpackSetFit(uint32_t display, const std::string& body, const Retail::BackpackFit& fit);
bool EsteriaBackpackFitInfo(uint32_t& display, std::string& body);
