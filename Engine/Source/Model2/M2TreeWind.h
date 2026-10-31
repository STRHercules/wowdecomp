#ifndef ENGINE_SOURCE_MODEL2_M2TREEWIND_H
#define ENGINE_SOURCE_MODEL2_M2TREEWIND_H

class C44Matrix;
class CM2Model;

// Not in the original binary. Placed trees and bushes sway in the wind: a small travelling-sine tilt about the model's
// base, applied to the world-view matrix the animate pass derives from the placement. The placement itself is left
// alone, so collision, selection and lighting are unchanged. Ported from wxl-treewind (GPL-3.0-or-later, WarcraftXL).
// The treeWind* / bushWind* cvars hold the settings; the "Esteria Tree Wind" video options page edits them.
namespace TreeWind {

void RegisterCVars();

// Called right after model->m_matrixF4 = m_worldTransform * view: rewrites it with the swayed placement when the
// model is a tree or bush that should move.
void Apply(CM2Model* model, const C44Matrix& view);

}  // namespace TreeWind

#endif
