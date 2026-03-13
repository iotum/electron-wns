{
  "targets": [
    {
      "target_name": "electron_wns",
      "sources": [
        "src/addon.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
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
            "windowsapp.lib"
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
