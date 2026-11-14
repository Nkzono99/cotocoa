program installed
  use iso_c_binding
  use cotocoa
  implicit none
  type(ctca_assembler) :: a
  call a%open(2,1024_c_int64_t)
  call a%finish()
  call a%close()
  print *, 'PASS installed Fortran API'
end program
