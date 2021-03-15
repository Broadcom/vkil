VKIL README
=============

VKIL is an interface library to communicate with Valkyrie based cards.

VKIL converts the functions into messages to be either read/write to/by
the hardware. These messages typically contain offload commands to perform
audio, video, crypto, or other data and processor intensive operations.

## Valkyrie Hardware Capabilities

The Valkyrie video transcoding ASICs have the following main logic blocks:
* Decoder – Accepts compressed video (H.264 / H.265 / VP9); outputs
  uncompressed RAW video stream
* Scaler – Allow to resize the video
* Encoders – Outputs compressed video (H.264 / H.265 / VP9)
* Quality measurement – Logic to measure the degradation in video quality
  after the encoding step
* PHYs – PCIe for the server and DDR for the memory
* Controller — General-purpose block that runs the firmware and coordinates
  the transcoding flow

## Libraries

* `libvkil` implements the host interface for Valkyrie base cards.

## Tools

* [vkflash_util] utility to access QSPI flash on Valkyrie card
* [vkpcie_util] utility to plot PCIe eye diagram for Valkyrie card
* [unittest] unitest tools to test VK host driver and DMA loopback

## Documentation

The documentation is available in the **doxy/** directory.

## License

VKIL codebase is licensed under the Apache License Version 2.0.
Please refer to the LICENSE.md file for detailed information.
