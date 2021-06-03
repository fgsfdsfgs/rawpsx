.set noreorder

.section .text

# copy of memcpy from PSn00bSDK's libc, but operating on words
# byte count and addresses must be a multiple of 4
# Arguments: 
#    a0 - destination address
#    a1 - source adress
#    a2 - bytes to copy
.global memcpy_w
.type memcpy_w, @function
memcpy_w:
  move  $v0, $a0
.Lcpy_loop:
  blez  $a2, .Lcpy_exit
  addi  $a2, -4
  lw    $a3, 0($a1)
  addiu $a1, 4
  sw    $a3, 0($a0)
  b     .Lcpy_loop
  addiu $a0, 4
.Lcpy_exit:
  jr    $ra
  nop

# copy of memset from PSn00bSDK's libc, but operating on words
# byte count and address must be a multiple of 4
# Arguments:
#    a0 - address to buffer
#    a1 - value to set
#    a2 - bytes to set
.global memset_w
.type memset_w, @function
memset_w:
  move  $v0, $a0
  blez  $a2, .Lset_exit
  addi  $a2, -4
  sw    $a1, 0($a0)
  b     memset_w
  addiu $a0, 4
.Lset_exit:
  jr    $ra
  nop
