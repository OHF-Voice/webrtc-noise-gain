# webrtc-noise-gain

Tiny Python wrapper around [webrtc-audio-processing](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/) for noise suppression and auto gain only.


## Install

``` sh
pip install webrtc-noise-gain
```


## Usage


``` python
from webrtc_noise_gain import AudioProcessor

# 0 = disable
auto_gain_dbfs = 3  # [0, 31]

# 0 = disable, 4 is max suppression
noise_suppression_level = 2  # [0, 4]

# 16 Khz mono with 16-bit samples only
audio_processor = AudioProcessor(auto_gain_dbfs, noise_suppression_level)

# Operates on 10ms of audio at a time (160 samples @ 16Khz)
audio_bytes_10ms = ...  # 160 samples (320 bytes)

result = audio_processor.Process10ms(audio_bytes_10ms)

if result.is_speech:
    # True if VAD detected speech

# result.audio contains clean audio
```


## Building

``` sh
# Install system dependencies
sudo apt-get install python3 python3-dev python3-pip python3-venv build-essential

# Clone repo
git clone http://github.com/rhasspy/webrtc-noise-gain.git
cd webrtc-noise-gain

# Create virtual environment
python3 -m venv .venv
source .venv/bin/activate
pip3 install --upgrade pip
pip3 install --upgrade wheel build

# Build
python3 -m build --wheel
find dist/
```
