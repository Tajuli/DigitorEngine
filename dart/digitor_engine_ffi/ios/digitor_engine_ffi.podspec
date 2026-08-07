Pod::Spec.new do |s|
  s.name = 'digitor_engine_ffi'
  s.version = '0.0.1'
  s.summary = 'DigitorEngine Flutter FFI integration.'
  s.description = 'Builds and links the DigitorEngine C ABI into iOS applications.'
  s.homepage = 'https://github.com/Tajuli/DigitorEngine'
  s.license = { :type => 'MIT', :file => '../../../LICENSE' }
  s.author = { 'DigitorEngine' => 'hello@primedigitor.com' }
  s.source = { :path => '.' }
  s.platform = :ios, '13.0'
  s.source_files = '../../../include/**/*.{h,hpp}', '../../../src/**/*.{c,cc,cpp,m,mm}'
  s.public_header_files = '../../../include/digitor/**/*.h'
  s.pod_target_xcconfig = { 'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20', 'DEFINES_MODULE' => 'YES' }
  s.frameworks = 'Metal', 'MetalKit', 'CoreVideo', 'CoreMedia', 'VideoToolbox', 'AudioToolbox'
  s.static_framework = true
end
