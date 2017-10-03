#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <mpi.h>

#include "./gol_lib/gol_array.h"
#include "./gol_lib/functions.h"

#define DEBUG 1
#define PRINT_INITIAL 0
#define PRINT_STEPS 0
#define DEFAULT_N 420
#define DEFAULT_M 420

void gol_array_read_file_and_scatter(char* filename, gol_array* gol_ar, int processors,
										int rows_per_block, int cols_per_block, int blocks_per_row);
void gol_array_generate_and_scatter(gol_array* gol_ar, int processors,
										int rows_per_block, int cols_per_block, int blocks_per_row);

int main(int argc, char* argv[])
{
	//the following can be also given by the user (to do)
	int N,M;
	int max_loops = 200;

	gol_array* temp;//for swaps;
	gol_array* ga1;
	gol_array* ga2;
	int i, j;

	/* SECTION A
		MPI Initialize
		Blocks computation
		Matrix allocation, initialization and 'scatter' to processes
		Column derived data type
	*/

	//MPI init
	char* filename;
	int my_rank, processors;
	int tag = 201049;
	MPI_Status status;
	int coordinates[2];

	MPI_Init (&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &processors);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  	//Read matrix size and game of life grid
  	//Or use default values and randomly generate a game if no arguments are given
	if (argc != 3 && argc != 4)
	{
		N = DEFAULT_N;
		M = DEFAULT_M;

		if (my_rank == 0)
		{
			printf("Running with default matrix size\n");
			printf("Usage 1: 'mpiexec -n <process_num> ./gol_mpi <filename> <N> <M>'\n");
			printf("Usage 2: 'mpiexec -n <process_num> ./gol_mpi <N> <M>'\n");
		}
	}
	else
	{
		N = atoi(argv[argc-2]);
		M = atoi(argv[argc-1]);

		if (my_rank == 0)
		{
			if (N == 0 || M == 0)
			{
				printf("Invalid arguments given!");	
				printf("Usage 1: 'mpiexec -n <process_num> ./gol_mpi <filename> <N> <M>'\n");
				printf("Usage 2: 'mpiexec -n <process_num> ./gol_mpi <N> <M>'\n");
				printf("Aborting...\n");
				MPI_Abort(MPI_COMM_WORLD, -1);
			}
		}
	}


	if (my_rank == 0)
		printf("N = %d\nM = %d\n", N, M);

	//Calculate block properties
	int processors_sqrt = sqrt(processors);
	int rows_per_block = N / processors_sqrt;
	int cols_per_block = M / processors_sqrt;
	int blocks_per_row = N / rows_per_block;
	int blocks_per_col = M / cols_per_block;

	//allocate and init two NxM gol_arrays
	ga1 = gol_array_init(N, M);
	ga2 = gol_array_init(N, M);

	//read/generate game of life array
	//'scatter' game matrix (send the coordinates to the correct process)
  	if (my_rank == 0) 
  	{

		if (argc == 2 || argc == 4) 
		{
			filename = argv[1];
			gol_array_read_file_and_scatter(filename, ga1, processors, rows_per_block, cols_per_block, blocks_per_row);
		}
		else 
		{//no input file given, generate a random game array 
			printf("No input file given as argument\n");
			printf("Generating a random game of life array to play\n");
			gol_array_generate_and_scatter(ga1, processors, rows_per_block, cols_per_block, blocks_per_row);
		}
		
		if (PRINT_INITIAL)
		{
			printf("Printing initial array:\n\n");
			print_array(ga1->array, N, M);
		}
		
		putchar('\n');	
		printf("Starting the Game of Life\n");

  	}
  	else //for other processes besides master
  	{
  		//receive coordinates of alive cells, until we receive (-1,-1)
  		short int** array = ga1->array;
  		int finished = 0;

  		while (!finished)
  		{
  			MPI_Recv(coordinates, 2, MPI_INT, 0, tag, MPI_COMM_WORLD, &status);

  			if (coordinates[0] == -1 && coordinates[1] == -1)
  			{
  				finished = 1;
  			}
  			else
  			{
  				array[ coordinates[0] ][ coordinates[1] ] = 1;
  			}
  		}
  	}

	//Define our column derived data type
	MPI_Datatype derived_type_column;

	MPI_Type_vector(N,    
	   1,                  
	   M,         
	   MPI_SHORT,       
	   &derived_type_column);       

	MPI_Type_commit(&derived_type_column);


	/* SECTION B
		Create our virtual topology
		For the current proccess in the new topology
			Find the ranks of its 8 neighbours
			Calculate its piece/boundaries of the game matrix 
	*/

  	//Create our virtual topology
  	MPI_Comm virtual_comm;
  	int ndimansions, reorder;
  	int dimansion_size[2], periods[2];
  	ndimansions = 2;
  	dimansion_size[0] = blocks_per_row;
  	dimansion_size[1] = blocks_per_col;
  	periods[0] = 1;
  	periods[1] = 1;
  	reorder = 1;

  	int ret = MPI_Cart_create(MPI_COMM_WORLD, ndimansions, dimansion_size, periods, reorder, &virtual_comm);

  	//find neighbour processes ranks
  	int rank_u, rank_d, rank_r, rank_l;
  	int rank_ur,  rank_ul ,rank_dr, rank_dl;
  	int neighbour_coords[2];
  	int my_coords[2];

  	if (ret == MPI_COMM_NULL) 
  	{
  		printf("Process %d wont be included in this game of life.\n", my_rank);
  		printf("The grid is smaller than available processes!\n");
  		printf("Aborting.. Try different numbers for better results!\n");
  		MPI_Abort(MPI_COMM_WORLD, -2);
  	}
  	else //get this process's rank and coords
  	{
  		//get this process's coordinates in the virtual topology
  		ret = MPI_Cart_coords(virtual_comm, my_rank, ndimansions, my_coords);

  		printf("Process %d : My coords are %d - %d\n", my_rank, my_coords[0], my_coords[1]);

  		//get the ranks of this process's (8) neighbours
  		neighbour_coords[0] = my_coords[0] - 1;
  		neighbour_coords[1] = my_coords[1];
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_u);

  		neighbour_coords[0] = my_coords[0] + 1;
  		neighbour_coords[1] = my_coords[1];
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_d);

  		neighbour_coords[0] = my_coords[0];
  		neighbour_coords[1] = my_coords[1] + 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_r);

  		neighbour_coords[0] = my_coords[0];
  		neighbour_coords[1] = my_coords[1] - 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_l);

  		neighbour_coords[0] = my_coords[0] - 1;
  		neighbour_coords[1] = my_coords[1] + 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_ur);

  		neighbour_coords[0] = my_coords[0] - 1;
  		neighbour_coords[1] = my_coords[1] - 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_ul);

  		neighbour_coords[0] = my_coords[0] + 1;
  		neighbour_coords[1] = my_coords[1] + 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_dr);

  		neighbour_coords[0] = my_coords[0] + 1;
  		neighbour_coords[1] = my_coords[1] - 1;
  		ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &rank_dl);

  		if (DEBUG)
	  	{
	  		printf("Process %d : My coords are %d - %d\nNeighbours of %d: %d %d %d %d %d %d %d %d", my_rank, my_coords[0], my_coords[1], my_rank, rank_u, rank_d, rank_r, rank_l, rank_ur, rank_ul, rank_dr, rank_dl);
	  	}
	}

  	//Calculate this process's boundaries
	int row_start, row_end, col_start, col_end;
  	
  	//we use the process's coordinates instead of its rank
  	//because mpi might have reordered the processes for better performance (virtual topology)
  	row_start = my_coords[0] * rows_per_block;
  	row_end = row_start + rows_per_block;
  	col_start = my_coords[1] * cols_per_block;
  	col_end = col_start + cols_per_block;

	//DEBUG PRINT FOR BOUNDARIES (only master prints it)
	if (DEBUG)
  	{
		if (my_rank == 0)
		{
			int proccess_rank;
			int row_start, row_end, col_start, col_end;
			int i, j;
	  		
	  		for (i = 0; i < blocks_per_row; i++)
	  		{
	  			for (j = 0; j < blocks_per_col; j++)
	  			{
				  	row_start = i * rows_per_block;
				  	row_end = row_start + rows_per_block;
				  	col_start = j * cols_per_block;
				  	col_end = col_start + cols_per_block;

				  	neighbour_coords[0] = i;
				  	neighbour_coords[1] = j;
				  	ret = MPI_Cart_rank(virtual_comm, neighbour_coords, &proccess_rank);
			
					printf("%d: rows [%d-%d) cols [%d-%d)\n", proccess_rank, row_start, row_end, col_start, col_end);
	  			}
	  		}
		}
	}

  	//DEBUG PRINT FOR ARRAY SCATTER
  	if (DEBUG)
  	{
  		char outname[20];
  		sprintf(outname, "test_outs/out_%d", my_rank);

  		FILE* out = fopen(outname, "w");

	  	fprintf(out, "PROCESS %d ARRAY\n", my_rank);
	  	int r,c;

		for (r=row_start; r<row_end; r++)
		{
			fputc('|', out);

			for (c=col_start; c<col_end; c++)
			{
				if (ga1->array[r][c] == 1)
					fputc('o', out);
				else
					fputc(' ', out);

				fputc('|', out);
			}

			fputc('\n', out);
		}

		fclose(out);
	}

	MPI_Barrier(MPI_COMM_WORLD);

	/* SECTION C
		Game of life loop
			x8 MPI_ISend
			x8 MPI_IRecv
			Calculate 'inner' cells
			Wait for IRecvs to complete
			Calculate 'outer' cells
			MPI_Reduce for master to see if there was a change or not
			If (change && repeats < max_repeats)
				repeat
			else
				finish
	*/

	int count = 0;
	int no_change;

	long int start = time(NULL);

	while (count < max_loops) 
	{
		count++;
		no_change = 1;
		short int** array1 = ga1->array;
		short int** array2 = ga2->array;

		//8 Isend
		//8 IRecv


		for (i=row_start + 1; i<row_end - 1; i++) {
			for (j= col_start + 1; j< col_end; j++) {
				//for each cell/organism

				//for each cell/organism
				//see if there is a change
				//populate functions applies the game's rules
				//and returns 0 if a change occurs
				if (populate(array1, array2, N, M, i, j) == 0)
				{
					no_change = 0;
				}	
			}
		}

		//only the master process prints
		if (PRINT_STEPS && my_rank == 0) {
			print_array(array2, N, M);
			putchar('\n');
		}

		if (no_change == 1) {
			printf("Terminating because there was no change at loop number %d\n", count);
			break;
		}

		//swap arrays (array2 becomes array1)
		temp = ga1;
		ga1 = ga2;
		ga2 = temp;

	}


	if (my_rank == 0) 
	{
		if (no_change == 0)
		{
			printf("Max loop number (%d) was reached. Terminating Game of Life\n", max_loops);
		}

		printf("Time elapsed: %ld seconds\n", time(NULL) - start);
	}

	//free arrays
	gol_array_free(&ga1);
	gol_array_free(&ga2);

	MPI_Finalize();

	return 0;

}


void gol_array_read_file_and_scatter(char* filename, gol_array* gol_ar, int processors
										,int rows_per_block, int cols_per_block, int blocks_per_row)
{
	short int** array = gol_ar->array;
	int N = gol_ar->lines;
	int M = gol_ar->columns;
	int tag = 201049;

	char line[100];
	char copy[100];
	int counter = 0;
	int successful = 0;
	int row_of_block;
	int col_of_block;
	int destination_process;
	int coordinates[2];

	FILE* file = fopen(filename, "r");

	if (file == NULL) 
	{
		printf("Error opening file\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}

	while (1)
	{
		counter++;

		if (fgets(line, 100, file) == NULL)
			break;

		/*ignore blank lines or lines that start with '#'*/
		if (strlen(line) == 0 || line[0] == '#')
			continue;

		//the line should contain just 2 numbers
		//which are the coordinates (row column) of an alive organism/cell
		char* token;
		int row,col;

		strcpy(copy, line);//keep a copy of the actual line
		//strtok messes the string up.. 
		
		//get row (first token of line)
		token = strtok(line, " ");

		if (token == NULL)
		{
			printf("Skipping invalid line (%d): '%s'\n",counter, copy);
			continue;
		}

		row = atoi(token);

		//get column
		token = strtok(NULL, " ");

		if (token == NULL)
		{
			printf("Skipping invalid line (%d): '%s'\n",counter, copy);
			continue;
		}

		col = atoi(token);

		//ignore invalid lines (row or column out of bounds)
		if (row < 1 || row > N || col < 1 || col > M)
		{
			printf("Invalid row or column\n");
			printf("Skipping invalid line (%d): '%s'\n",counter, copy);
			continue;
		}

		successful++;

		row_of_block = (row - 1) / rows_per_block;
		col_of_block = (col - 1) / cols_per_block;
		destination_process = row_of_block * blocks_per_row + col_of_block;
		// printf("(%d,%d) should go to process %d\n", row-1,col-1,destination_process);

		//if its for the master process
		if (destination_process == 0)
			array[row-1][col-1] = 1;
		else //if its for another process
		{
			//send the coordinates
			coordinates[0] = row - 1;
			coordinates[1] = col - 1;
			MPI_Send(coordinates, 2, MPI_INT, destination_process, tag, MPI_COMM_WORLD);
		}

	}

	int i;
	coordinates[0] = -1;
	coordinates[1] = -1;

	//send 'ok' message to all processes
	for (i=1; i<processors; i++)
		MPI_Send(coordinates, 2, MPI_INT, i, tag, MPI_COMM_WORLD);


	printf("\nSuccesfully read %d coordinates\n", successful);
	fclose(file);
}


void gol_array_generate_and_scatter(gol_array* gol_ar, int processors,
										int rows_per_block, int cols_per_block, int blocks_per_row)
{
	char datestr[9];
	char timestr[7];

	get_date_time_str(datestr, timestr);
	char filename[36];
	sprintf(filename, "generated_tests/rga_%s_%s", datestr, timestr);
	
	FILE* file = fopen(filename, "w");
	if (file == NULL)
	{
		printf("gol_array_generate error opening file!\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}

	short int** array = gol_ar->array;
	int lines = gol_ar->lines;
	int columns = gol_ar->columns;

	srand(time(NULL));
	int alive_count = rand() % (lines*columns + 1);
	int i;
	int tag = 201049;
	int row_of_block;
	int col_of_block;
	int destination_process;
	int coordinates[2];

	for (i=0; i<alive_count; i++)
	{
		int x,y;

		x = rand() % lines;
		y = rand() % columns;

		fprintf(file, "%d %d\n", x, y);

		row_of_block = x / rows_per_block;
		col_of_block = y / cols_per_block;
		destination_process = row_of_block * blocks_per_row + col_of_block;
		// printf("(%d,%d) should go to process %d\n", x,y,destination_process);

		//if its for the master process
		if (destination_process == 0)
			array[x][y] = 1;
		else //if its for another process
		{
			coordinates[0] = x;
			coordinates[1] = y;
			MPI_Send(coordinates, 2, MPI_INT, destination_process, tag, MPI_COMM_WORLD);
		}

		array[x][y] = 1;
	}

	coordinates[0] = -1;
	coordinates[1] = -1;

	for (i=1; i<processors; i++)
		MPI_Send(coordinates, 2, MPI_INT, i, tag, MPI_COMM_WORLD);

	fclose(file);
}