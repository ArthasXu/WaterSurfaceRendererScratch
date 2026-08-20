Package: glm:x64-windows@1.0.3

**Host Environment**

- Host: x64-windows
- Compiler: MSVC 19.41.34120.0
- CMake Version: 4.4.0
-    vcpkg-tool version: 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8
    vcpkg-scripts version: 45f9f39362 2026-08-20 (12 hours ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Note: glm only supports static library linkage. Building static library.
Downloading https://github.com/g-truc/glm/archive/1.0.3.tar.gz -> g-truc-glm-1.0.3.tar.gz
error: curl operation failed with error code 56 (Failure when receiving data from the peer).
error: Not a transient network error, won't retry download from https://github.com/g-truc/glm/archive/1.0.3.tar.gz
note: If you are using a proxy, please ensure your proxy settings are correct.
Possible causes are:
1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to `https://address:port`.
This is not correct, because `https://` prefix claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr, etc...) is an HTTP proxy.
Try setting `http://address:port` to both HTTP_PROXY and HTTPS_PROXY instead.
2. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software. See: https://github.com/microsoft/vcpkg-tool/pull/77
The value set by your proxy might be wrong, or have same `https://` prefix issue.
3. Your proxy's remote server is out of service.
If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github.com/Microsoft/vcpkg/issues
CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:134 (message):
  Download failed, halting portfile.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_from_github.cmake:120 (z_vcpkg_download_distfile)
  buildtrees/versioning_/versions/glm/fbe815929bda5a29c7d1890ddae28f31e8fe4f5d/portfile.cmake:3 (vcpkg_from_github)
  scripts/ports.cmake:209 (include)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "dependencies": [
    "glfw3",
    "glm",
    "spdlog",
    "fftw3",
    {
      "name": "imgui",
      "features": [
        "glfw-binding",
        "vulkan-binding"
      ]
    },
    "stb"
  ],
  "builtin-baseline": "c1d80d9cb071c3f4a98c67c1196b137cc5b72918"
}

```
</details>
