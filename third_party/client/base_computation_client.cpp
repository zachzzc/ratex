#include "client/base_computation_client.h"

#include "lazy_tensor_core/csrc/compiler/backend_impl_interface.h"

#include "lazy_tensors/computation_client/nnc_computation_client.h"

#include "torch_mnm/csrc/compiler/utils.h"
#include "torch_mnm/csrc/compiler/mnm_lowering_context.h"
#include "torch_mnm/csrc/utils/file.h"

#include "mnm/serialization.h"

#include "env_vars.h"

namespace lazy_tensors {

using namespace torch_lazy_tensors::compiler;
using namespace mnm;

std::once_flag g_computation_client_once;
std::atomic<lazy_tensors::ComputationClient*> g_computation_client(nullptr);

ComputationClient* ComputationClient::Get() {
  return getBackendRegistrar()->GetComputationClient();
}

std::unique_ptr<ComputationClient> ComputationClient::Create() {
  LOG(FATAL) << "NotImplemented Error";
}

}  // namespace lazy_tensors

namespace torch_mnm {

using namespace lazy_tensors;

void PopulateLocalDevices(BaseComputationClient::Options* options) {
  auto dev_kind = sys_util::GetEnvString(torch_mnm::env::kEnvDefaultDevice, "CPU");
  int dev_id = 0;  // TODO: Determine the device ID using local rank.
  bool ignore = true;

  // Iterate candidate devices in the preferred order, and include all devices the
  // lower or equal ordinal of the user specified default device.
  for (auto kind : {"GPU", "CPU"}) {
    std::string ltc_device = dev_kind + ":" + std::to_string(dev_id);
    if (kind == dev_kind) {
      options->default_device = ltc_device;
      ignore = false;
    }
    if (!ignore) {
      options->devices.insert(ltc_device);
      options->global_device_map[ltc_device] =
          torch_lazy_tensors::compiler::mnm_backend::ToMNMDevice(ltc_device).c_str();
    }
  }
}

client::ShapeData BaseComputationClient::GetShapeData(const Shape& shape) {
  std::vector<int64_t> dimensions(shape.dimensions().begin(), shape.dimensions().end());
  PrimitiveType element_type = shape.element_type();
  std::vector<client::ShapeData> element_shapes;
  for (const Shape& element_shape : shape.tuple_shapes()) {
    element_shapes.push_back(GetShapeData(element_shape));
  }
  auto minor_to_major = shape.layout().minor_to_major();
  return client::ShapeData(element_type, dimensions, element_shapes,
                           std::vector<int64_t>(minor_to_major.begin(), minor_to_major.end()));
}

std::string BaseComputationClient::GetResourceDomain(const std::string& device) const {
  return "";
}

std::string BaseComputationClient::GetDefaultDevice() const {
  // TODO(@hzfan): Investigate whether we should use the LTC API to get the default device.
  // i.e., lazy_tensors::NNCComputationClient::HardwareDeviceType()
  return options_.default_device;
}

std::vector<std::string> BaseComputationClient::GetLocalDevices() const {
  return std::vector<std::string>(options_.devices.begin(), options_.devices.end());
}

std::vector<std::string> BaseComputationClient::GetAllDevices() const {
  std::vector<std::string> devices;
  for (const auto& dev_target : options_.global_device_map) {
    devices.push_back(dev_target.first);
  }
  return devices;
}

void BaseComputationClient::SetReplicationDevices(
    std::shared_ptr<std::vector<std::string>> devices) {
  LTC_CHECK_EQ(devices->size(), size_t(1)) << "Replication not supported yet";
}

std::shared_ptr<std::vector<std::string>> BaseComputationClient::GetReplicationDevices() {
  return nullptr;
}

void BaseComputationClient::PrepareToExit() {
}

std::vector<ComputationClient::ComputationPtr> BaseComputationClient::Compile(
    std::vector<ComputationClient::CompileInstance> instances) {
  std::vector<ComputationPtr> results;
  for (const auto& ins : instances) {
    if (options_.cache_enabled) {
      static auto query = registry::GetPackedFunc("torch_mnm.utils.cache.query");
      static auto create_entry = registry::GetPackedFunc("torch_mnm.utils.cache.create_entry");
      const auto& key = CompileCacheKey(ins);
      std::string dirname = query(key).operator std::string();
      if (PathExist(dirname)) {
        // Cache Hit
        std::ifstream compute_file(dirname + "/compute.json");
        std::string json = Load(compute_file);
        results.push_back(CompileDeSerialize(json));
      } else {
        // Cache Miss
        ComputationPtr res = Compile(ins);
        dirname = create_entry(key).operator std::string();
        std::ofstream compute_file(dirname + "/compute.json");
        std::string json = CompileSerialize(res);
        Save(compute_file, json);
        results.push_back(res);
      }
    } else {
      results.push_back(Compile(ins));
    }
  }
  return results;
}

ObjectRef BaseComputationClient::CompileCacheKey(CompileInstance instance) {
  auto* computation = static_cast<torch_lazy_tensors::
    compiler::mnm_backend::GenericComputationMNM*>(instance.computation.get());
  auto func = Downcast<Function>(computation->computation());
  Array<Integer> model_states;
  Map<Integer, Integer> alias;
  for (size_t i = 0; i < func->params.size(); ++i) {
    Var var = func->params[i];
    if (computation->model_states().find(var) != computation->model_states().end()) {
      model_states.push_back(i);
    }
  }
  for (const auto& kv : computation->alias()) {
    alias.Set(kv.first, kv.second);
  }

  String json(mnm::ir::serialization::SaveJSON(computation->computation()));

  return Array<ObjectRef>({
    json,
    model_states,
    alias
  });
}

}  // namespace torch_mnm