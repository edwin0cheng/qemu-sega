# QEMU fork with Sega Genesis support (without Z80)


``` bash
mkdir build
cd build
../configure --enable-sdl --disable-coreaudio --disable-cocoa --target-list=m68k-softmmu --disable-capstone --disable-pie --disable-slirp --disable-vmnet

# make with single process
make

# make with 6 core
make -j6

```

```bash
./m68k-softmmu/qemu-system-m68k -M sega-genesis -serial mon:stdio -singlestep -icount shift=7
```

## References
https://github.com/transistorfet/moa
https://jabberwocky.ca/posts/2021-11-making_an_emulator.html#simulating-the-cpu
https://jabberwocky.ca/posts/2022-01-emulating_the_sega_genesis_part1.html

http://jabberwocky.ca/posts/2022-01-emulating_the_sega_genesis_part1.html
https://sebastienbourdelin.com/2021/06/16/writing-a-custom-device-for-qemu/
http://souktha.github.io/software/qemu-port/
https://github.com/devos50/qemu-ios/blob/ipod_touch_2g/RUNNING.md

https://en.wikibooks.org/wiki/Genesis_Programming/68K_Memory_map/
https://segaretro.org/Control_Pad_(Mega_Drive)
https://segaretro.org/Sega_Mega_Drive/Memory_map
https://segaretro.org/Sega_Mega_Drive/Technical_specifications#Graphics

https://rasterscroll.com/mdgraphics/
http://gendev.spritesmind.net/forum/viewforum.php?f=2&sid=4e39d1baa804d0106850b2f57a6abc03
