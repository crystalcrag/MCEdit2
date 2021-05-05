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
