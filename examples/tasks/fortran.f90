program task_example
  use iso_c_binding
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: task
  type(ctca_ticket) :: tickets(10)
  real(c_double) :: input(3)
  real(c_double), allocatable :: received(:)
  integer :: step
  logical :: available
  character(len=64) :: name
  call get_command_argument(1,name)
  call ctx%open(trim(name),config='tasks.yaml')
  if(trim(name)=='producer') then
    do step=0,9
      input=real([step,step+1,step+2],c_double)
      call ctx%submit('sum',input,tickets(step+1),step=int(step,c_int64_t))
    end do
    do step=1,10
      call tickets(step)%wait()
      call tickets(step)%release()
    end do
  else
    do
      call ctx%next_task('sum',task,has_task=available)
      if(.not.available) exit
      call task%copy_to(received)
      print *, 'step=',task%step(),' sum=',sum(received)
      call task%complete()
    end do
  end if
  call ctx%close()
  print *, 'PASS task example Fortran'
end program
