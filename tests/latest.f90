program latest
  use iso_c_binding
  use mpi
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(c_double) :: field(3)
  real(c_double), allocatable :: received(:)
  integer :: rank,ierr,step,i,count,source,last(2)
  logical :: available
  character(len=1024) :: name,config
  interface
    function usleep(microseconds) bind(c,name='usleep') result(rc)
      import
      integer(c_int), value :: microseconds
      integer(c_int) :: rc
    end function
  end interface
  call get_command_argument(1,name)
  call get_command_argument(2,config)
  call ctx%open(trim(name),config=trim(config))
  call MPI_Comm_rank(ctx%comm(),rank,ierr)
  if(trim(name)=='producer') then
    do step=1,50
      do i=1,3
        field(i)=step*1000+rank*100+i-1
      end do
      call ctx%send('field',field,step=int(step,c_int64_t),offset=[rank*3_c_int64_t])
      field=0
    end do
  else
    last=0
    count=0
    do
      call ctx%receive('field',message,has_message=available)
      if(.not. available) exit
      call message%copy_to(received)
      source=message%source_rank()+1
      step=int(message%step())
      call require(source>=1 .and. source<=2,'source rank')
      call require(step>last(source) .and. message%sequence()==message%step(),'increasing sequence')
      do i=1,3
        call require(received(i)==step*1000+(source-1)*100+i-1,'snapshot')
      end do
      if(count==0) ierr=usleep(200000_c_int)
      last(source)=step
      count=count+1
      call message%complete()
    end do
    call require(all(last==50) .and. count<100,'drops and final frame')
    print *, 'PASS latest Fortran rank=',rank,' messages=',count
  end if
  call ctx%close()
contains
  subroutine require(condition,text)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: text
    if(.not. condition) then
      print *, 'FAIL latest: ',text
      call MPI_Abort(MPI_COMM_WORLD,1,ierr)
    end if
  end subroutine
end program latest
