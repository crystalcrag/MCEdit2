# MCEdit2
Voxel engine compatible with Minecraft 1.12

Honest try at recreating the creative mode of Minecraft 1.12, using as little dependencies as possible. So far, this are the ones required:
* **OpenGL 4.3**: this is not due to lazyness, but because this engine makes **HEAVY** use of instanced rendering for maximum performance, by using glMultiDrawArraysIndirect() which is available only in GLv4.3. Replacing this draw call with one compatible with GLv3 is not going to be easy (wihtout some serious performance drop).
* **SDL1**: why not SDL2 ? Because SDL1 is largely enough for this project.
* **SITGL**: that's a pretty big one. It is used to render/manage user interface (because doing this using only bare bone OpenGL calls is way above my tolerance to pain). SITGL only depends on OpenGL v3.
* **zlib**: needed for reading/writing NBT. Any version can be used.

Why 1.12 ? Because later versions have some significant changes behind the scenes (especially 1.13: the way blocks are stored on disk has changed, not including all the new meachanics introduced by this version). Supporting both is way too much work.

As of now, only Windows is supported, even though there are no major road blocks as long as those dependencies are available on your platform (which probably excludes Mac OS X, due to its poor OpenGL support). SITGL only supports Windows for now and is a fairly large dependency. This code base should be much more portable.

# Supported features

* **Voxel editing**: remove/add block one by one or affect whole region at once. Most of the 252 block types of Minecraft 1.12 are implemented.
* **Sub-block precision**: can generate voxel of less than one full block (ie: vertical/horizontal slab), works like the chisel tool of TerraFirmaCraft.
* **Dynamic lighting**: SkyLight and BlockLight tables update according to block properties (absortion and emission).
* **Redstone**: most of redstone devices are supported (torch, repeater, comparator, wire, piston, rails and slime block).
* **Minecart mechanics**: although compatibility with Minecraft is, at best, close enough.
* **Dynamic skydome**: night/day cycle kind of works, but only manually (use F5/F6 key to cycle).
* **Edit inventories**: chest, furnace, dropper, player, ender chest, even sign can be edited. Care has be given to make all interfaces as close as possible to Minecraft.

# Missing features
* **Entity/mobs**: they don't have any AI, they are just stationnary.
* **Random tick update**: not implemented on purpose, consider the world being frozen in time (unless triggered by an user action). That means no leave decay, no mob spawing, no crops growing, no trees poping...
* **Blocks not implemented**: banner.
* **Blocks with poor support**: chorus plant, trip wire, noteblock, command blocks: they are rendered but have no mechanic.
* **Terrain generation**: mimicking exactly what Minecraft does is way too much work, even though the engine easily supports custom terrain generators (check doc/internals.html).

# Comparison with Minecraft 1.12

Here are some screenshots of the engine in action. They were taken around feb 2022, the engine might have improved since.

First screenshot is from Minecraft, screenshot below is from this engine.

## Exterior

Block light (torches, redstone lanps, jack-o-lantern, ...) has a **much more pronounced yellow-ish tint**. For those who have the Minecraft lighting burnt on their retina, it might seems weird at first, but I once accidentally over-saturated the block light values in the fragment shader and really like the result ever since.

![Exterior, day, minecraft render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Day-Ext-MC.jpg)

Same camera position, but using a FOV of 80 instead of 70 for the screenshot above. Block face shading is also **dependant on sun direction** (unlike Minecraft which uses fixed shading per face).

![Exterior, day, MCEditv2 render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Day-Ext-MCEdit.jpg)

## Interior

That lighting change is particularly dramatic on interior scene. The interior looks a bit too dull in the Minecraft: over-saturating block light gives a much cozier look and feel, while not sacrificing visibility by making things too dark:

![Interior, night, minecraft render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Night-Int-MC.jpg)

MCEditv2 rendering:

![Interior, night, MCEditv2 render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Night-Int-MCEdit.jpg)

## Interior / exterior mixed

Another interior screenshot from the room above the previous screenshot. This one has a mix of Skylight/blocklight values all around. The screenshot was taken at sunset.

![Interior, night, minecraft render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World-Night-Int2-MC.jpg)

MCEditv2 rendering:

![Interior, night, MCEditv2 render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World-Night-Int2-MCEdit.jpg)

## Exterior sunset

This engine implements a not-too-bad looking skydome. The clouds need improvements though: they are simply a low-res texture mapped onto the skydome. Future version will at least use some kind of fractal brownian motion (FBM) cloud system. If I have enough motivation, I might look at volumetric clouds:

![Exterior, sunset, minecraft render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Sunset-Ext-MC.jpg)

MCEditv2 rendering (render distance here is 16 chunks):

![Exterior, sunset, MCEditv2 render](https://raw.githubusercontent.com/crystalcrag/WikiResources/main/World1-Sunset-Ext-MCEdit.jpg)

