# Under GNU GPL
# 2010 niekt0@hysteria.sk 
#
# This code should be copied to some free page,
# together with own page tables

# XXX Cloned from i386, not working !!! XXX


# Registers:
#
# EAX : dynamic
# EBX : not used
# ECX : "del_bytes", offset to delete (in 4-bytes just before stos)
# EDX : "end", end of area that should be deleted
# ESI : "start", start of area that should be deleted
# EDI : "del_from", address, where deleting should start in single page
#

eraser_start:
.globl eraser_start
delall:

mov (%rbp),%rsi	# start of area
add $8,%rbp
mov (%rbp),%rdx	# end of area
add $8,%rbp

endlessloop:
cmp $0,%rdx		# if this is last area (end in 0)
je endlessloop		# just loop

delarea:		# delete one area

# exp: map 1 page
# XXX 64
mov %rsi,%rax		# address to delete
mov $0xFFFFFFFFFF000, %rbx 	# hack, cannot and directly
and %rbx,%rax		# only bites 51-12(so this address is 4KB aligned)
add $0x01F,%rax 	# special bits
mov %rax,0x0020	# write to PGD. XXX where is it mapped?
# we will write to linear address 0x4000, =>

invlpg 0x4000	# invalidate "rewrite" page 
	
delpage:

mov $0x4000,%rdi	# address to rewrite, change is done by mapping 
mov $0x200,%rcx		# 4KB to delete, divided by 8 (stosq) 0x1000 / 8 = 200

# just delete page now
# using stosq should be faster than using normal loop
# rewrite rcx "8-bytes" starting from rdi with value of rax

mov $0xADDEADDEADDEADDE,%rax	# rewrite with DEADDEADDEADDEAD ;)
#XXX shr $3,%rcx		# stosl erases 8 bytes per step, so divide by 8
rep stosq		# loop itself

add $0x1000, %rsi	# proceed next page
cmp %rdx,%rsi		# end of area? 
jl delarea		# end > start, so erase next page

jmp delall		# proceed next area

eraser_end:
.globl eraser_end
