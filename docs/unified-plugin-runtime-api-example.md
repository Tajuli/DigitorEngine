# Unified runtime integration example

The Digitor host binds the existing production runtimes once:

```cpp
UnifiedPluginRuntime runtime({
  [&](const PluginZeroCopyRequest& request, std::string* diagnostic) {
    return gpu_program_runtime.dispatch(request, diagnostic);
  },
  [&](const PluginTransitionRequest& request, std::string* diagnostic) {
    return transition_program_runtime.dispatch(request, diagnostic);
  }
});
```

The app then submits `UnifiedPluginRequest` values for built-in and imported packages without branching on individual plugin IDs.
