let version = "1.0.4191.47"
let package_path = "vendor/WebView2.nupkg"
let include_path = "vendor/WebView2/build/native/include/WebView2.h"

mkdir vendor

if not ($package_path | path exists) {
    curl.exe -L --fail --output $package_path $"https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/($version)"
}

if not ($include_path | path exists) {
    mkdir vendor/WebView2
    ^tar -xf $package_path -C vendor/WebView2
}
