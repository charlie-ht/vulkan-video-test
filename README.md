Decode a single AVC IDR frame for testing purposes.

# Build the test
: cmake -B build -S .

# Run the test
For NVIDIA
: ./build/vvp --device-name=nvidia

For AMD
: ./build/vvp --device-name=amd

For Intel
: ./build/vvp --device-name=amd

You may also use `--device-major-minor=MAJOR.MINOR` to select based on DRM
properties, or `--driver-version=X.Y.Z` to select based on enabled
driver (for multi-driver systems).



