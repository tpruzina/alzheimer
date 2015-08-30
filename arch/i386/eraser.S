# Under GNU GPL
# 2010 niekt0@hysteria.sk 

# ix86 gas asm
#
# This code should be copied to some free page,
# together with own page tables

# Registers:
#
# EAX : dynamic
# EBX : dynamic
# ECX : "del_bytes", offset to delete (in 4-bytes just before stos)
# EDX : "end", end of area that should be deleted
# ESI : "start", start of area that should be deleted
# EDI : "del_from", address, where deleting should start in single page
#

eraser_start:
.globl eraser_start
delall:

# please note that addreses are shift 12 bits to the right (4KB)
# this is because we want to support addresses over 4GB even on i386 
# so addreses are stored in multiplies of 4KB

movl (%ebp),%esi #start of area
add $4,%ebp
movl (%ebp),%edx #end of area
add $4,%ebp

endlessloop:
cmp $0,%edx     # if this is last area (end in 0)
je endlessloop  # just loop

delarea:		# delete one area

# exp: map 1 page
# next 4 lines are > 4GB hack
movl %esi,%ebx		# temp register
shr $7,%ebx		# extending addres bits are in strange order
and $0x001FF000,%ebx	# bits 20-13
#shl 12,%esi		# shift address back, as if nothing happened.

movl %esi,%eax		# address to delete
shl $12,%eax		# shift address back,
and $0xFFC00000,%eax	# only first 10 bites (so this address is 4MB aligned)
add $0x1EFF,%eax 	# special bits
movl %eax,0xFF0		# write to PGD. 
# we will write to linear address 0xFF000000, =>
# 11 1111 1100 (0xFF.. when in the beginning of int) is 0x3FC =>
# 0x3fc * 4 (4 bytes per pointer) = 0xFF0

invlpg 0xFF000000	# invalidate PGD
	

delpage:

# exp: start = start + 4M
#add $0x400000,%esi	#if address + 4MB page is still in range. Old value is not needed.
add $0x400,%esi		#if address + 4MB page is still in range. Old value is not needed.

# exp: del_from = 0xFF000000 + start & 0x3FFFFF
movl $0xFF000000,%edi	# address to rewrite, constant except offset, change is done by mapping 
movl %esi,%eax		# we need offset in this page, because area can be 4KB, not 4MB aligned
shl $12,%eax		# shift address back,
and $0x3FFFFF,%eax	# -||-
add %eax,%edi		# final offset

#exp: del_bytes = 4MB - del_from
movl $0x400000,%ecx	# 4MB to delete
movl %edi,%eax 		# only offset bytes
and $0x3FFFFF,%eax 	# -||-
sub %eax,%ecx		# to avoid overlapping

# exp: if start > end 
cmp %edx,%esi		
jle delpage_in_range	# if pages are not overlapping, continue

# exp: del_bytes = del_bytes - (start - end)
movl %esi,%eax		# start
sub %edx,%eax		# start - end
shl $12,%eax		# shift address back,
sub %eax, %ecx		# del_bytes - (start - end)

delpage_in_range:

# just delete page now
# using stosd should be faster than using normal loop
# rewrite ecx "4-bytes" starting from edi with value of eax

movl $0xADDEADDE,%eax	# rewrite with DEADDEAD ;)
inc %ecx		# fix, without this we would leave last word intact
shr $2,%ecx		# stosl erases 4 bytes per step, so divide by 4
rep stosl		# loop itself

#and $0xFFC00000,%esi	# point start to aligned 4MB page, without offset.
and $0xFFFFFC00,%esi	# point start to aligned 4MB page, without offset.

cmp %edx,%esi		# end of area? 
jl delarea		# end > start, so erase next page

jmp delall		# proceed next area

eraser_end:
.globl eraser_end
