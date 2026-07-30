#include "digitor/production_node_graph.hpp"
#include "digitor/native_node_backend_runtime.hpp"
#include "digitor/native_node_pipeline_runtime.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace {
bool near(float a,float b,float e=1.0e-6f){return std::abs(a-b)<=e;}
}

void test_node_system() {
  using namespace digitor;
  const std::vector<Color> source{{1,0,0,1},{0,1,0,1},{1,0,0,1},{0,1,0,1}};

  PrimaryWheelsDescriptor wheels_desc;
  wheels_desc.offset={0.4f,0.0f,0.0f};
  const auto wheels=PrimaryWheelsParameters::create(wheels_desc);

  QualifierSettings qualifier_settings;
  qualifier_settings.hue={0.95f,0.05f,0.02f};
  qualifier_settings.saturation={0.5f,1.0f,0.05f};
  const auto red=HslQualifierParameters::create(qualifier_settings);

  ProductionNodeGraph mask_first;
  const auto a=mask_first.add_serial_after(mask_first.input_node(),"Mask First");
  mask_first.select_node(a);
  mask_first.add_operation_to_selected(make_hsl_qualifier_operation(red));
  mask_first.add_operation_to_selected(make_primary_wheels_operation(wheels));

  ProductionNodeGraph mask_last;
  const auto b=mask_last.add_serial_after(mask_last.input_node(),"Mask Last");
  mask_last.select_node(b);
  mask_last.add_operation_to_selected(make_primary_wheels_operation(wheels));
  mask_last.add_operation_to_selected(make_hsl_qualifier_operation(red));

  const auto first=mask_first.render(source,2,2);
  const auto last=mask_last.render(source,2,2);
  assert(first.size()==last.size());
  for(std::size_t i=0;i<first.size();++i){
    assert(near(first[i].r,last[i].r));
    assert(near(first[i].g,last[i].g));
    assert(near(first[i].b,last[i].b));
  }
  assert(first[0].r>source[0].r);
  assert(near(first[1].r,source[1].r));

  ProductionNodeGraph multiplied;
  const auto c=multiplied.add_serial_after(multiplied.input_node(),"Combined Masks");
  multiplied.select_node(c);
  PowerWindowSettings half_a;half_a.shape=WindowShape::rectangle;half_a.width=2;half_a.height=2;half_a.feather=0;half_a.opacity=.5f;
  PowerWindowSettings half_b=half_a;
  multiplied.add_operation_to_selected(make_power_window_operation(half_a));
  multiplied.add_operation_to_selected(make_primary_wheels_operation(wheels));
  multiplied.add_operation_to_selected(make_power_window_operation(half_b));
  const auto combined=multiplied.render(source,2,2);
  const float full_delta=apply_primary_wheels_reference(source[0],*wheels).r-source[0].r;
  assert(near(combined[0].r-source[0].r,full_delta*.25f,2.0e-5f));

  ProductionNodeGraph editable;
  const auto d=editable.add_serial_after(editable.input_node(),"Editable");
  editable.select_node(d);
  editable.set_operation_on_selected(make_primary_wheels_operation(wheels));
  PrimaryWheelsDescriptor replacement_desc;replacement_desc.offset={.1f,0,0};
  editable.set_operation_on_selected(make_primary_wheels_operation(PrimaryWheelsParameters::create(replacement_desc)));
  assert(editable.node(d).operations.size()==1);
  assert(editable.set_operation_enabled(d,NodeOperationKind::primary_wheels,false));
  const auto disabled=editable.render(source,2,2);
  assert(near(disabled[0].r,source[0].r));
  assert(editable.set_operation_enabled(d,NodeOperationKind::primary_wheels,true));
  assert(editable.remove_operation(d,NodeOperationKind::primary_wheels));
  assert(editable.node(d).operations.empty());

  for (const auto backend : {DIGITOR_RENDERER_VULKAN, DIGITOR_RENDERER_D3D12,
                             DIGITOR_RENDERER_METAL, DIGITOR_RENDERER_OPENGL_ES}) {
    for (const auto kernel : {NativeNodeKernel::parallel_mixer,
                              NativeNodeKernel::hsl_matte,
                              NativeNodeKernel::power_window_matte,
                              NativeNodeKernel::matte_multiply,
                              NativeNodeKernel::masked_composite}) {
      const auto contract = native_node_pipeline_contract(backend, kernel);
      assert(validate_native_node_pipeline_contract(contract));
      NativeNodeDispatchResources resources;
      resources.kernel = kernel;
      resources.constants.resize(contract.constant_bytes);
      for (std::uint32_t i = 0; i < contract.binding_count; ++i) {
        const auto& binding = contract.bindings[i];
        if (binding.kind != NativeNodeBindingKind::constants)
          resources.textures.push_back({binding.binding, 100u + binding.binding, 2, 2});
      }
      std::string diagnostic;
      assert(validate_native_node_dispatch_resources(contract, resources, diagnostic));
      resources.kernel = kernel == NativeNodeKernel::parallel_mixer
          ? NativeNodeKernel::matte_multiply
          : NativeNodeKernel::parallel_mixer;
      NativeNodeBackendRuntime runtime({}, {}, {}, {});
      assert(!runtime.dispatch(backend, kernel, 2, 2, 1, resources, diagnostic));
      assert(diagnostic == "native dispatch kernel identity mismatch");
    }
  }

  ProductionNodeGraph parallel;
  const auto serial=parallel.add_serial_after(parallel.input_node(),"Serial");
  const auto pair=parallel.add_parallel_after(serial,"A","B");
  parallel.remove_node(pair.second);
  assert(parallel.node(pair.first).kind==ProductionNodeKind::serial);
  assert(parallel.render(source,2,2).size()==source.size());
}
