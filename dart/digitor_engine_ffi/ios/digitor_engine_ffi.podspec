Pod::Spec.new do |s|
  s.name = 'digitor_engine_ffi'
  s.version = '0.0.1'
  s.summary = 'DigitorEngine Flutter native texture host.'
  s.description = 'Registers production CVPixelBuffer-backed DigitorEngine preview textures with Flutter on iOS.'
  s.homepage = 'https://github.com/Tajuli/DigitorEngine'
  s.license = { :type => 'MIT', :file => '../../../LICENSE' }
  s.author = { 'DigitorEngine' => 'hello@primedigitor.com' }
  s.source = { :path => '.' }
  s.platform = :ios, '13.0'
  s.source_files = 'Classes/**/*.{h,m,mm}'
  s.public_header_files = 'Classes/**/*.h'
  s.dependency 'Flutter'
  s.frameworks = 'CoreVideo', 'CoreMedia', 'Metal'
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'DEFINES_MODULE' => 'YES'
  }
  s.requires_arc = true
  s.static_framework = true
end
