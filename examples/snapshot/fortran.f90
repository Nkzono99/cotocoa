program snapshot
  use iso_c_binding, only: c_double, c_int64_t
  use mpi
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(c_double) :: field(3,5,7)
  real(c_double), allocatable :: received(:,:,:)
  integer :: rank, ierr, step
  logical :: available
  character(len=64) :: name

  call get_command_argument(1, name)
  call ctx%open(trim(name))
  call MPI_Comm_rank(ctx%comm(), rank, ierr)
  if (trim(name) == 'producer') then
    do step = 1, 10
      field = real(step+rank,c_double)
      call ctx%send('potential', field, step=int(step,c_int64_t), &
                    offset=[int(rank*3,c_int64_t),0_c_int64_t,0_c_int64_t])
    end do
  else
    do
      call ctx%receive('potential', message, has_message=available)
      if (.not. available) exit
      call message%copy_to(received)
      print *, 'step=',message%step(),' source=',message%source_rank(), &
               ' mean=',sum(received)/size(received)
      call message%complete()
    end do
  end if
  call ctx%close()
end program snapshot
