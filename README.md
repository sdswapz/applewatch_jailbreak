# This is an Apple Watch jailbreak file

Here's the argument structure (controlled from userspace)
```
  struct mach_voucher_extract_attr_recipe_args {
    PAD_ARG_(mach_port_name_t, voucher_name);
    PAD_ARG_(mach_voucher_attr_key_t, key);
    PAD_ARG_(mach_voucher_attr_raw_recipe_t, recipe);
    PAD_ARG_(user_addr_t, recipe_size);
  };
```
recipe and recipe_size are userspace pointers.

At point (a) four bytes are read from the userspace pointer recipe_size into sz.

At point (b) if sz was less than MACH_VOUCHER_ATTR_MAX_RAW_RECIPE_ARRAY_SIZE (5120) and greater than MACH_VOUCHER_TRAP_STACK_LIMIT (256)
sz is used to allocate a kernel heap buffer.

At point (c) copyin is called again to copy userspace memory into that buffer which was just allocated, but rather than passing sz (the 
validate size which was allocated) args->recipe_size is passed as the size. This is the userspace pointer *to* the size, not the size!

This leads to a completely controlled kernel heap overflow.

Tested on MacOS Sierra 10.12.1 (16B2555)
 
Yeah, it's not too user-friendly to add support for other devices, sorry!

```
To add support for another device you have to update these constants:

  kaslr_shift = vtable - 0xFFFFFFF006FA2C50;

  kernel_base = 0xFFFFFFF007004000 + kaslr_shift;
  get_metaclass = 0xFFFFFFF0074446DC + kaslr_shift;
  osserializer_serialize = 0xFFFFFFF00745B0DC + kaslr_shift;
  ret = 0xFFFFFFF0074446E4 + kaslr_shift;
  kernel_uuid_copy = 0xFFFFFFF0074664F8 + kaslr_shift;
```

The first one is the address of the AGXCommandQueue vtable; I seem to remember this one is the trickiest to find without extra tooling as there's no symbol for it.

Find the only cross reference to the string "AGXCommandQueue". It should be in an InitFunc which is initializing that class's OSMetaClass object in the bss. The first argument passed to the OSMetaClass constructor is its address; if you look at the xrefs to *that* address you'll be able to find the metaclass's ::alloc method which will call AGXCommandQueue's operator new. You should see the size (0xDB8) being passed as the first argument to the first function call (which is called the allocator). You should then be able to find the store of the vtable pointer to the object and update the first constant.

The kernel base should remain the same.

get_metaclass in this case is the address of OSData::getMetaClass; there's a symbol for this.

osserializer_serialize is the symbol OSSerializer::serialize

ret is just any RET instruction

kernel_uuid_copy is the symbol uuid_copy
```
There are a few reasons why the exploit could fail; most likely is that the heap groom failed (it's not a very advanced heap groom.) For devices with more RAM try increasing the iteration count in this loop to eg 10000:

  for (int i = 0; i < 2000; i++){
    prealloc_port(prealloc_size);
  }
```
Also try rebooting the phone; leave it for a couple of minutes then try the exploit once.


*** the exploit ***

I target preallocated mach message buffers which are allocated via kalloc. The first 4 bytes are a size field which is used to determine
where in the buffer to read and write a message. By corrupting this field we can cause mach messages to be read and written outside the bounds of
the kalloc allocation backing the kmsg.

There is a slight complication in that a port's preallocated kmsg will only be used for actual mach_msg sends by the kernel (not for replies
to MIG methods for example.) This makes it a bit trickier to get enough controlled content in them.

One type of mach message which the kernel sends with a lot of user-controlled data is an exception message, sent when a thread crashes.

The file load_regs_and_crash.s contains ARM64 assembly which loads the ARM64 general purpose registers with the contents of a buffer
such that when it crashes the exception message contains that data buffer (about 0x70 bytes are controlled.)

By overwriting the port's ikm_size field to point to the header of another port we can read and write another port's header and learn where it is
in memory. We can then free that second port and reallocate a user client in its place which we can also read and write.

I read the userclients vtable pointer then use the OSSerializer::serialize gadget technique as detailed in
[https://info.lookout.com/rs/051-ESQ-475/images/pegasus-exploits-technical-details.pdf] to call an arbitrary function with two controlled arguments.

I call uuid_copy which calls memmove(arg0, arg1, 0x10). By pointing either arg0 or arg1 into the userclient itself (which we can read by receiving the
exception message) we can read and write arbitrary kernel memory in 16 byte chunks.
