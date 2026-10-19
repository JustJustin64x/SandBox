#ifndef SKILL_TREE_UI_H
#define SKILL_TREE_UI_H

#include "common.h"
#include <string>

class Tribe;
class SkillTree;
class POIManager;
class AgeManager;

class SkillTreeUI {
public:
    SkillTreeUI();

    void Update(Tribe& tribe, Vector2 mousePos, bool mouseClicked);
    void Draw(const Tribe& tribe, Vector2 mousePos) const;

    bool IsOpen() const { return isOpen_; }
    void SetOpen(bool open) { isOpen_ = open; }
    void Toggle() { isOpen_ = !isOpen_; }

private:
    void DrawNodeCard(const struct TechNode& node, Rectangle rect, bool isHovered, bool isFocused, float time) const;
    void DrawTooltip(const struct TechNode& node, const POIManager& pois, Vector2 mousePos) const;
    void DrawConnectionLines(const SkillTree& skills, Rectangle eraBounds[3]) const;

    bool isOpen_ = false;
    int hoveredNodeId_ = -1;
    float animTimer_ = 0.0f;
};

#endif // SKILL_TREE_UI_H

