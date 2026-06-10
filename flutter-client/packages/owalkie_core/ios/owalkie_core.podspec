#
# CocoaPods spec for owalkie_core FFI plugin (CMake → static lib, force-loaded).
# Full relay session on iOS: set OWALKIE_FLUTTER_FULL_SESSION=ON and install
# vcpkg ios deps (see flutter-client/ios/README.md).
#
Pod::Spec.new do |s|
  s.name             = 'owalkie_core'
  s.version          = '0.0.1'
  s.summary          = 'O-Walkie owalkie-core FFI plugin'
  s.description      = <<-DESC
FFI plugin wrapping owalkie-core for Flutter (session transport + miniaudio).
                       DESC
  s.homepage         = 'https://github.com/outsidepro-arts/O-Walkie'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'O-Walkie' => 'denis.outsidepro@gmail.com' }
  s.source           = { :path => '.' }
  s.dependency 'Flutter'
  s.platform         = :ios, '13.0'

  s.script_phase = {
    :name => 'Build owalkie_core native (CMake)',
    :script => '"${PODS_TARGET_SRCROOT}/build_native.sh"',
    :execution_position => :before_compile,
    :input_files => [
      '${PODS_TARGET_SRCROOT}/build_native.sh',
      '${PODS_TARGET_SRCROOT}/../src/CMakeLists.txt',
    ],
    :output_files => ['${PODS_TARGET_SRCROOT}/build/libowalkie_core.a'],
  }

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'OTHER_LDFLAGS' => '$(inherited) -force_load "${PODS_TARGET_SRCROOT}/build/libowalkie_core.a"',
    'USER_HEADER_SEARCH_PATHS' => '"${PODS_TARGET_SRCROOT}/../src" "${PODS_TARGET_SRCROOT}/../../../../owalkie-core/include"',
  }

  s.swift_version = '5.0'
end
