#include "digitor/windows_zero_copy_media_host.hpp"
#include "digitor/windows_zero_copy_rollout.hpp"

#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

int main(int argc,char** argv){
  if(argc<4){
    std::cerr<<"usage: digitor_windows_zero_copy_media_cli <media> <nv12|p010> <report.json>\n";
    return 64;
  }
#ifndef _WIN32
  std::cerr<<"Windows only\n";return 2;
#else
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory6> factory;
  if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)))){std::cerr<<"DXGI factory failed\n";return 2;}
  ComPtr<IDXGIAdapter1> adapter;
  for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;++i){
    DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
    if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE){adapter.Reset();continue;}
    ComPtr<ID3D12Device> probe;
    if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&probe))))break;
    adapter.Reset();
  }
  if(!adapter){std::cerr<<"compatible hardware adapter unavailable\n";return 2;}
  ComPtr<ID3D12Device> device;
  if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){std::cerr<<"D3D12 device failed\n";return 2;}

  digitor::WindowsZeroCopyMediaHostOptions options;
  options.media_path=argv[1];options.report_path=argv[3];options.strict_gpu_first=true;
  options.complete_validation=true;
  const std::string format=argv[2];
  options.require_nv12=format=="nv12";options.require_p010=format=="p010";
  if(!options.require_nv12&&!options.require_p010){std::cerr<<"format must be nv12 or p010\n";return 64;}

  digitor::WindowsZeroCopyMediaHost host(device.Get(),options);
  std::string diagnostic;
  auto result=host.open(&diagnostic);
  if(result!=DIGITOR_RESULT_OK){std::cerr<<diagnostic<<"\n";return 2;}
  digitor::WindowsZeroCopyThresholds thresholds;
  digitor::WindowsZeroCopyQualificationReport report;
  result=host.qualify(thresholds,report,&diagnostic);
  std::cout<<digitor::windows_zero_copy_report_json(report);
  const auto rollout=digitor::evaluate_windows_zero_copy_rollout(
      digitor::WindowsZeroCopyRolloutMode::production,argv[3],true);
  std::cerr<<diagnostic<<"; "<<rollout.diagnostic<<"\n";
  return result==DIGITOR_RESULT_OK&&rollout.production_ready?0:3;
#endif
}
