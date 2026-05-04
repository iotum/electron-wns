{
  "targets": [
    {
      "target_name": "electron_wns",
      "sources": [
        "src/electron_wns.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "winappsdk-headers"
      ],
      "defines": [
        "NAPI_CPP_EXCEPTIONS"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "conditions": [
        ["OS=='win'", {
          "libraries": [
            "runtimeobject.lib",
            "windowsapp.lib",
            "<(module_root_dir)/winappsdk-headers/lib/x64/Microsoft.WindowsAppRuntime.lib",
            "<(module_root_dir)/winappsdk-headers/lib/x64/Microsoft.WindowsAppRuntime.Bootstrap.lib"
          ],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "AdditionalOptions": [
                "/EHsc"
              ]
            }
          }
        }]
      ]
    }
  ]
}
