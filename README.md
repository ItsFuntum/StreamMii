# StreamMii

StreamMii is a Wii U plugin that captures and streams the TV or DRC (GamePad) display to a receiver over the network.

> [!CAUTION]
> The plugin can use a significant amount of system memory at higher capture resolutions. This is amplified when using JPEG compression, as the output is RGB888 instead of RGB565. You can reduce the capture resolution at any time using the **Decrease Capture Resolution** button combo (default: **TV + ZL**).

## Installation

(`[ENVIRONMENT]` is a placeholder for the actual environment name.)

1. Copy the file  `StreamMii.wps` into `sd:/wiiu/environments/[ENVIRONMENT]/plugins`.
2. Requires the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) in `sd:/wiiu/environments/[ENVIRONMENT]/modules`.

## Usage

> [!NOTE]
> To run the receiver, you will need the following Python packages:
> * NumPy
> * OpenCV
> * lz4

After installation:

1. Start the receiver using the appropriate command for your operating system.

   **Windows:**
   ```bash
   py receiver.py
   ```
   **Linux:**
   ```bash
   python3 receiver.py
   ```

1. Start the Wii U using your environment (for example, Aroma) and open the config menu.

2. Navigate to **Network Settings** inside of **StreamMii** and set the octets to your device's local IP Address:
  
   For example, if the device's local IP address is **192.168.1.98**, set:
   * First Octet to `192`
   * Second Octet to `168`
   * Third Octet to `1`
   * Fourth Octet to `98`

5. Start the application or game you want to stream.

## Features

* Capture target can be set to **TV** or **DRC (GamePad)**
* Capture resolutions:

  * 160x90
  * 320x180
  * 480x270
  * 640x360
  * 854x480
* Maximum frame rates:

  * 1 FPS
  * 5 FPS
  * 10 FPS
  * 15 FPS
  * 20 FPS
  * 30 FPS
  * 60 FPS
* LZ4 and JPEG compression (LZ4 outputs RGB565 and JPEG outputs RGB888)
* Adjustable JPEG quality
* Optional LZ4 delta encoding
* Configurable button combos for changing the capture resolution

## Button Combos

The button combos used to change the capture resolution can be configured in the StreamMii config menu.

The default button combos are:

* **TV + ZL**: Decrease capture resolution
* **TV + ZR**: Increase capture resolution

## Known Issues

* Performance and stream quality isn't great. There are no plans to improve this due to the lack of hardware encoding.

* StreamMii does not stream certain applications and applets, such as Miiverse. Opening the HOME Menu or WUPS Config Menu also pauses the stream. Additionally, some games require workarounds for streaming. For example, **Super Smash Bros. for Wii U** requires the TV to be set to 720p mode for the plugin to stream correctly.

* Some applications may produce different colors in the stream compared to what is displayed on the Wii U.

## Building

For building you need:

* [wups](https://github.com/Maschell/WiiUPluginSystem)
* [wut](https://github.com/devkitpro/wut)

Install these dependencies according to their respective README files. Make sure to also install any dependencies required by the libraries themselves.

Once the dependencies are installed, StreamMii can be compiled with:

```bash
make
```

For a debug build with logging enabled:

```bash
make DEBUG=1
```

## Building Using Docker

It's possible to use a docker image for building. This way you don't need anything installed on your host system.

Build the Docker image (only required once):

```bash
docker build . -t streammii-builder
```

Build StreamMii with logging enabled:

```bash
docker run -it --rm -v ${PWD}:/project streammii-builder make DEBUG=1
```

Clean the build files:

```bash
docker run -it --rm -v ${PWD}:/project streammii-builder make clean
```

## Formatting the Code Using Docker

The following command formats all `.cpp`, `.hpp`, and `.h` files in `src`, while excluding files under `src/libs/`:

```bash
find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) ! -path 'src/libs/*' -print0 |
xargs -0 docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$PWD:/src" \
    -w /src \
    ghcr.io/wiiu-env/clang-format:13.0.0-2 \
    -i
```
