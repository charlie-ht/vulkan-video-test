Decode a single AVC IDR frame for testing purposes. 

Only tested on Linux, it has a single dynamic dependency on `libvulkan.so`.

# Build the test

    cmake -B build -S .

# Run the test

    ./build/vvp --device-name=nvidia|amd|intel

You may also use `--device-major-minor=MAJOR.MINOR` to select based on DRM
properties, or `--driver-version=X.Y.Z` to select based on enabled
driver (for multi-driver systems).



