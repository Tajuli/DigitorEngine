#include "digitor/production_node_graph.hpp"
#include "digitor/native_node_backend_runtime.hpp"
#include "digitor/native_node_pipeline_runtime.hpp"
#include "digitor/filter.hpp"

#include <cassert>
#include <cmath>
#include <set>
#include <vector>

namespace {
bool near(float a,float b,float e=1.0e-6f){return std::abs(a-b)<=e;}
}

void test_node_system() {
  using namespace digitor;
  const std::vector<Color> source{{1,0,0,1},{0,1,0,1},{1,0,0,1},{0,1,0,1}};

  // Clean-room short-form-editor-style filter library: stable IDs, categories,
  // intensity mixing, stacking, serialization, alpha preservation, and
  // CPU/GPU command-path parity.
  FilterRegistry filters;
  assert(filters.presets().size() >= 28);
  std::set<std::string> filter_ids;
  for (const auto& preset : filters.presets()) {
    assert(!preset.id.empty() && !preset.name.empty());
    assert(filter_ids.insert(preset.id).second);
  }
  assert(filters.find("cinema_warm"));
  assert(filters.find("teal_amber"));
  assert(filters.find("skin_glow"));
  assert(filters.find("neon_city"));
  assert(!filters.category(FilterCategory::cinematic).empty());
  assert(!filters.category(FilterCategory::portrait).empty());

  const Color filter_input{0.25f,0.5f,0.75f,0.37f};
  const auto* warm_filter=filters.find("cinema_warm");
  assert(warm_filter);
  const auto bypassed=apply_filter(filter_input,*warm_filter,0.0f);
  assert(near(bypassed.r,filter_input.r)&&near(bypassed.g,filter_input.g)&&near(bypassed.b,filter_input.b));
  const auto filtered=apply_filter(filter_input,*warm_filter,1.0f);
  assert(!near(filtered.r,filter_input.r)||!near(filtered.g,filter_input.g)||!near(filtered.b,filter_input.b));
  assert(near(filtered.a,filter_input.a));

  FilterStack filter_stack;
  assert(filter_stack.add({"cinema_warm",0.65f,true}));
  assert(filter_stack.add({"soft_fade",0.25f,true}));
  assert(filter_stack.add({"mono_soft",0.0f,true}));
  const auto serialized_filters=filter_stack.serialize();
  const auto restored_filters=FilterStack::deserialize(serialized_filters);
  assert(restored_filters&&restored_filters->entries().size()==3);
  std::array<Color,2> filter_source{{filter_input,{0.8f,0.2f,0.1f,0.8f}}};
  std::array<Color,2> cpu_filter_output{},gpu_filter_output{};
  apply_filter_stack_cpu(filter_source.data(),cpu_filter_output.data(),filter_source.size(),filters,*restored_filters);
  CommandBuffer filter_commands;
  CommandEncoder filter_encoder(filter_commands);
  apply_filter_stack_gpu(filter_encoder,filter_source.data(),gpu_filter_output.data(),filter_source.size(),filters,*restored_filters);
  filter_encoder.finish();
  CommandQueue filter_queue;
  filter_queue.submit(filter_commands);
  for(std::size_t i=0;i<filter_source.size();++i){
    assert(near(cpu_filter_output[i].r,gpu_filter_output[i].r));
    assert(near(cpu_filter_output[i].g,gpu_filter_output[i].g));
    assert(near(cpu_filter_output[i].b,gpu_filter_output[i].b));
    assert(near(cpu_filter_output[i].a,filter_source[i].a));
  }
  assert(!FilterStack::deserialize("invalid"));

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
      bool has_sampled_input=false;
      for(std::uint32_t i=0;i<contract.binding_count;++i)
        has_sampled_input=has_sampled_input||contract.bindings[i].kind==NativeNodeBindingKind::sampled_or_storage_input;
      if(backend==DIGITOR_RENDERER_OPENGL_ES&&has_sampled_input){
        assert(contract.source.find("sampler2D")!=std::string_view::npos);
        assert(contract.source.find("texelFetch")!=std::string_view::npos);
        assert(contract.source.find("uniform readonly highp image2D")==std::string_view::npos);
      }
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
