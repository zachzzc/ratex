#include "./value.h"

namespace mnm {
namespace value {

using namespace mnm::ir;

ClosureValueExt ClosureValueExt::make(ir::Map<ir::Var, Value> env, IRModule mod, GlobalVar gvar) {
  auto ptr = make_object<ClosureValueExtObj>();
  ptr->env = env;
  ptr->mod = mod;
  ptr->gvar = gvar;
  return ClosureValueExt(ptr);
}

MNM_REGISTER_OBJECT_REFLECT(ClosureValueExtObj);

}  // namespace value
}  // namespace mnm
