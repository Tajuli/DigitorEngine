#include "digitor/windows_zero_copy_rollout.hpp"

#include <cassert>
#include <fstream>

int main(){
  using namespace digitor;
  auto disabled=evaluate_windows_zero_copy_rollout(
      WindowsZeroCopyRolloutMode::disabled,"",true);
  assert(disabled.effective==WindowsZeroCopyRolloutMode::disabled);

  auto missing=evaluate_windows_zero_copy_rollout(
      WindowsZeroCopyRolloutMode::production,"missing-report.json",true);
  assert(!missing.production_ready);
  assert(missing.effective==WindowsZeroCopyRolloutMode::disabled);

  const char* path="windows-zero-copy-pass.json";
  {std::ofstream f(path);f<<"{\"production_ready\": true}";}
  auto loose=evaluate_windows_zero_copy_rollout(
      WindowsZeroCopyRolloutMode::production,path,false);
  assert(loose.effective==WindowsZeroCopyRolloutMode::disabled);
  auto strict=evaluate_windows_zero_copy_rollout(
      WindowsZeroCopyRolloutMode::production,path,true);
  assert(strict.production_ready);
  assert(strict.effective==WindowsZeroCopyRolloutMode::production);
  std::remove(path);
  return 0;
}
