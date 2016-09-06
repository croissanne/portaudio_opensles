Android implementation for portaudio using opensles.



Misc
----
audio fast track:
      Opensl ES on android has an audio_output_fast_flag which will be automatically set
      when you open a device with the right parameters. When this flag is set latency
      will be reduced dramatically between opensl es and the android HAL.
      However this depends entirely on the device, for instance on a motorola moto g
      you need to play audio at 48kHz to get the fast flag, 
      and thus the lowest possible latency for that device.
