/* backprop-lmk.c

 * Simple implementation of the back-propagation training rule for a two-layer
 * network learning the "lmk setting parameters" function.  Includes a momentum
 * parameter, "alpha", and a "flatspot elimination" parameter, "c". Although the
 * flatspot elimination parameter is not strictly necessary, it can speed
 * learning, and is vitally important for the operation of backpropagation on
 * real-world problems.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Constant definitions */

#define INSIZE		4			/* Number of units in input layer  */
#define HDSIZE		4			/* Number of units in hidden layer */
#define OPSIZE		8			/* Number of units in output layer */


/* Note - both the input and the hidden layers have an additional "bias" weight
 * which is connected to a "unit" whose output is always one --- this "unit" is
 * kept in element 0 of the input and hidden layer weight arrays, and the active
 * units index from 1 to INSIZE, and 1 to HDSIZE respectively.  For the sake of
 * simplicity and uniformity, the output layer ignores element 0 of its array 
 * and indexes from 1 to OPSIZE.

 * The number of training and testing patterns are given by the predefined
 * size of the lowmemorykiller database. The global arrays "patterns" and
 * "desired" are used to hold the training patterns/responses and also the
 * test patterns/responses. The idea is that the network is first completely
 * trained, then these arrays are overwritten with test data/responses and the
 * network is then tested on these previously unseen patterns.

 * Since there are more training patterns than test patterns, the
 * arrays are dimensioned with the number of training patterns.
*/

#define TRAINING_PATTERNS  2400      /* Number of training patterns      */
#define TESTING_PATTERNS   2400      /* Number of testing patterns       */

#define TARGET_ERROR       1e-3      /* Average squared output error     */
                                     /* allowed in trained network.      */

#define MAX_EPOCHS         20000     /* This is the training limit       */

#define DELTA			0.2f			/* Tolerance on outputs, to be      */
										/* considered as 0, output has to   */
					/* be <= DELTA, to be considered as */
					/* 1, output >= 1.0 - DELTA.        */

#define CLASS              1         /* Codes for routine          */
#define MISCLASS           2         /* "report_outputs"                 */
#define INDEF              3

/* Global variable storage */

double  hidden[HDSIZE+1],             /* Hidden layer outputs */
       output[OPSIZE+1],             /* Output layer outputs */
       w_ih[INSIZE+1][HDSIZE+1],     /* Weight matrix from input to hidden layers */
       w_ho[HDSIZE+1][OPSIZE+1],     /* Weight matrix from hidden to output layers */
       dw_ih[INSIZE+1][HDSIZE+1],    /* Changes in w(i,j) matrix */
       dw_ho[HDSIZE+1][OPSIZE+1],    /* Changes in w(j,k) matrix */
       delta_p_output[OPSIZE+1],     /* Delta(k) values on presentation p */
       delta_p_hidden[HDSIZE+1],     /* Delta(j) values on presentation p */
       pattern[TRAINING_PATTERNS][INSIZE+1],  /* Set of training/testing patterns to be applied to the network */
       desired[TRAINING_PATTERNS][OPSIZE+1],  /* Set of desired responses to the training/testing patterns */
       eta,                          /* Learning rate parameter */
       alpha,                        /* Momentum parameter */
       c;                            /* Flatspot elimination parameter */

/* Function prototypes */

void  load_patterns(int total, char *filename);
void  load_pattern(int index, char *linebuffer);
void  load_initial_weights(void);
void  train_network(void);
float train_epoch(void);
float forwardprop(int p);
void  backprop(int p);
float random_val(void);
void  read_parameters(int argc, char *argv[]);
float read_argument(char *error_message, char *argument_string);

void  report_operation(void);
void  report_outputs(int p, int errortype);
void  dump_weights(void);
int   mismatch(int p);
int   match(int p);



/* The "main" function sets up the initial weights of the network and the training patterns.  It
   also reads (from the command line) values of eta (learning rate) and alpha (momentum) appropriate 
   to this training run.  The training is all performed by "train_network".
*/

int main(int argc, char *argv[])
{
    read_parameters(argc, argv);
    load_patterns(TRAINING_PATTERNS, "lowmemorykiller.tra");
    load_initial_weights();
    printf("Network parameters:  Input layer size  = %3d\n", INSIZE);
    printf("                     Hidden layer size = %3d\n", HDSIZE);
    printf("                     Output layer size = %3d\n", OPSIZE);
    printf("Training network\n\n");
    train_network();
    printf("Training done\nTesting network\n\n");
    load_patterns(TESTING_PATTERNS, "lowmemorykiller.tes");
    report_operation();
    dump_weights();
    return 0;
}



/* Routines to read the network parameters, eta, alpha and c, from the command line */

void read_parameters(int argc, char *argv[]) 
{
    if ( argc != 4 )  {
        fprintf(stderr, "%s, three command line arguments expected.\n", argv[0]);
        fprintf(stderr, "    <eta> <alpha> <c>\n");
	exit(1);
    }
    eta   = read_argument("1, (eta)",   argv[1]);
    alpha = read_argument("2, (alpha)", argv[2]);
    c     = read_argument("3, (c)",     argv[3]);
}

float read_argument(char *error_message, char *argument_string)
{
    float parameter;
    
    if ( 1 != sscanf(argument_string, "%f", &parameter) ) {
        fprintf(stderr, "Argument %s is not a float \"%s\"\n", 
	                error_message, argument_string);
	exit(1);
    }
    return parameter;
}


/* This routine trains the network by performing multiple backpropagation passes until the error
   is within the desired limits or the network training has gone on too long without any progress
   being made (MAX_EPOCHS limit exceeded).  Note that the network starts with a hugh initial
   value of "epoch_error" to force at least one backpropagation training cycle to occur.

   Note that training aborts if the epoch error starts to rise.
*/

void train_network(void)
{
	int epoch = 0;
	float epoch_error = 1e12f, old_epoch_error = 1e13f;

	while ( epoch_error > TARGET_ERROR && epoch < MAX_EPOCHS )  {
		epoch++;
		epoch_error = train_epoch();
		if ( epoch % 100 == 0 ) {
			printf("epoch %6d, epoch_error %f\n", epoch, epoch_error);
			/*if ( epoch_error - old_epoch_error > 0.0f ) {
				printf("Epoch error is INCREASING, aborting training\n");
			return;
			}
			else old_epoch_error = epoch_error;
      */
		}
	}
	if ( epoch >= MAX_EPOCHS && epoch_error > TARGET_ERROR )
		printf("%d training epochs failed to train network, error remains at %f\n\n",
			epoch, epoch_error);
	else {
		printf("Network trained in %d epochs\n", epoch);
		printf("Mean squared error on training data = %f (target %f)\n", 
		       epoch_error, TARGET_ERROR);
		printf("Learning rate parameter (eta)       = %f\n", eta);
		printf("Momentum parameter (alpha)          = %f\n", alpha);
		printf("Flatspot elimination parameter (c)  = %f\n\n", c);
	}
}


/* This routine performs one "epoch's" worth of training by performing backpropagation 
   passes over the network for various training patterns.  

   In this implementation, training patterns on this epoch are chosen randomly from 
   the training set. The idea is to remove any correlation artifacts that might arise 
   from applying the same sequence of training patterns on successive epochs.

   "Testing", to return average error on this epoch, is performed in a similar random
   way.

   Note that half the total set of training patterns is used for training on any one
   epoch, and half for "testing".  There will, of course, be random overlap between the
   training and "testing" data.

*/

float train_epoch(void)
{
    int p, n = TRAINING_PATTERNS / 4;
    float error = 0.0f;

    for ( p = 0; p < n; p++ )
    	backprop( rand() % TRAINING_PATTERNS );
    for ( p = 0; p < n; p++ )  
        error += forwardprop( rand() % TRAINING_PATTERNS );
    return error / (float) n;
}



/* This routine applied input pattern number "p" to the network and calculates the error
   between the training (i.e., desired) ouput and the actual output 
   (0.5 * sigma(t(p,k) - o(p,k))**2)).  This error value is returned to the caller.
*/

float forwardprop(int p)
{
    int i, j, k;
    float temp, error;

    /* This is the input to hidden layer forward propagation.  Note how the current
       input layer is picked up from the 2-d array of training patterns. */
    for ( j = 1; j <= HDSIZE; j++ )
    	hidden[j] = 0.0f;
    for ( i = 0; i <= INSIZE; i++ )  {
      temp = pattern[p][i];
			for ( j = 1; j <= HDSIZE; j++ )
				hidden[j] += w_ih[i][j] * temp;
    }
    
    /* This is the hidden to output layer forward propagation. */
    for ( k = 1; k <= OPSIZE; k++ )
    	output[k] = 0.0f;
    for ( j = 0; j <= HDSIZE; j++ )  {
      temp = hidden[j];
			for ( k = 1; k <= OPSIZE; k++ )
				output[k] += w_ho[j][k] * temp;
    }


    /* Now we calculate the normalize error between the actual and desired outputs 
    and report this back to the caller. */
    error = 0.0f;
    for ( k = 1; k <= OPSIZE; k++ )  {
      temp = (desired[p][k] - output[k]) / desired[p][k];
			error += temp * temp;
    }
    error *= 0.5f;
    return error;
}

/* This is the function which actually performs the backpropagation training passes.
   For a given pattern vector, indexed by "p", it will calculate the changes to the
   weights of the hidden and output layers for each weight in the network.  The weight
   changes are held in the arrays "dw_ih" and "dw_ho", thus the weight change in the
   previous training cycle is always available to act as a momentum term.

   Note the operation of the flatspot elimination parameter "c".  This is added to
   the derivatives of the activation function and has the effect of tending to move the
   system on when the derivatives are very small (i.e., a flat spot).

   Hence, the equations for the deltas are modified to:

   (a) for an output layer neuron:

   \delta_k = (t_{pk} - o_{pk})(f'(y_{pk}) + c) 
            = (t_{pk} - o_{pk})(1 + c)

   (b) for a hidden layer neuron:

   \delta_j = (f'(y_{pj}) + c) \sum_k \delta_{pk} w_{jk}
            = (1 + c) \sum_k \delta_{pk} w_{jk}
   
*/

void backprop(int p)
{
    int i, j, k;
    float temp;

    forwardprop(p);  /* Needed to set the output values on the hidden and output layer
                        neurons for the training input indexed by "p". */

    /* Calcualte the Delta(p,k) values (deltas for output layer neurons) for this
       presentation, "p". */
    for ( k = 1; k <= OPSIZE; k++ )  {
      temp = output[k];
			delta_p_output[k] = (desired[p][k] - temp) * (1.0f + c);
    }

    /* Now use these Delta(p,k) values to calculate the Delta(p,j) values.  Note that
       the Delta(p,j) values are calculated before any changes are made to the /2
       hidden to output layer weights. */
    for ( j = 1; j <= HDSIZE; j++ )  {
        for ( temp = 0.0f, k = 1; k <= OPSIZE; k++ )  
        	temp += w_ho[j][k] * delta_p_output[k];
			delta_p_hidden[j] = temp * (1.0f + c);
    }

    /* Only now may we perform weight updates on the hidden to output layer weights. */
    for ( j = 0; j <= HDSIZE; j++ )
  		for ( k = 1; k <= OPSIZE; k++ )  {
  		    dw_ho[j][k] = eta * hidden[j] * delta_p_output[k] + alpha * dw_ho[j][k - 1];
  		    w_ho[j][k] += dw_ho[j][k];
  	}

    /* And on the input to hidden layer weights. */
    for ( i = 0; i <= INSIZE; i++ )
  		for ( j = 1; j <= HDSIZE; j++ )  {
  		    dw_ih[i][j] = eta * pattern[p][i] * delta_p_hidden[j] + alpha * dw_ih[i][j - 1];
  		    w_ih[i][j] += dw_ih[i][j];
		}
}


/* Here is where the network training/test patterns and desired responses are loaded
   into an internal array for access.
*/

void load_patterns(int total, char *filename)
{
	FILE *pattern_file;
	char linebuffer[1024];
	int i, j;

	if ( NULL == ( pattern_file = fopen(filename, "r") ) )  {
		fprintf(stderr, "Fatal Error: in load_patterns, can't open %s for input, halting\n", filename);
		exit(1);
	}
	else {
		for ( i = 0; i < total; i++ ) {
			if ( NULL == fgets(linebuffer, 1024, pattern_file) )  {
    			fprintf(stderr, "Fatal Error: in load_patterns, can't read pattern %d, expected %d, halting\n", 
        	        i, total);
                exit(1);
			}
			else load_pattern(i, linebuffer);
		}
	}
}

/* Load a single training/test pattern from a character buffer into pattern[index]
   and desired[index].

   These are loaded into indices 1..INSIZE of the pattern indexed by parameter
   "index".
*/

void load_pattern(int index, char *linebuffer)
{
	int i;
	char *p;

	p = linebuffer;
	for ( i = 1; i <= INSIZE; i++ )  {
		pattern[index][i] = atof(p);
		while ( *p != ',' && *p != '\n' && *p != '\0' )
			p++;
		if ( *p == ',' )
			p++;
	}
    for ( i = 1; i <= OPSIZE; i++ )  {
        desired[index][i] = atof(p);
        while ( *p != ',' && *p != '\n' && *p != '\0' )
            p++;
        if ( *p == ',' )
            p++;
    }
}

/* Here the initial weight vectors are established.  Note especially how the bias "units" in
   each layer (array index 0 in the input and hidden layers) are set up with an output value
   of 1.  Since biases and weights are being treated uniformly as weights, it is important to 
   remember that the bias "units" need to be initialised.
*/

void load_initial_weights(void)
{
	int p, i, j, k;

	/* Initialise bias "neurons" to output 1's, note how this has to be done for all the
	   input patterns, as these serve in turn as the input layer. */
	for ( p = 0; p < TRAINING_PATTERNS; p++ ) pattern[p][0] = 1.0f;
	hidden[0] = 1.0f;

	/* Initialise the weights to small random values. */
	for ( i = 0; i <= INSIZE; i++ )
		for ( j = 1; j <= HDSIZE; j++ )  {
		    w_ih[i][j] = random_val();
		    dw_ih[i][j] = 0.0f;
		}

    for ( j = 0; j <= HDSIZE; j++ )
		for ( k = 0; k <= OPSIZE; k++ )  {
			w_ho[j][k] = random_val();
			dw_ho[j][k] = 0.0f;
		}
}


/* Generate a random number uniformly distributed in the range -0.5...0.5. */

float random_val(void)
{
	return (float) rand() / (float) RAND_MAX - 0.5f;
}

/* Report status information about the network to the user. This assumes that 
   the test patterns and desired responses have been loaded.

*/

void report_operation(void)
{
    int p, misclassifications, good_classifications;

    misclassifications = 0;
    good_classifications = 0;
    for ( p = 0; p < TESTING_PATTERNS; p++ )  {
      forwardprop( p );
    	if ( mismatch(p) ) {
    	  misclassifications++;
        report_outputs( p, MISCLASS );
    	}
    	else if ( match(p) ) {
    	  good_classifications++;
        report_outputs( p, CLASS );
      }
      else {
        report_outputs( p, INDEF );
      }
    }

    printf("%4d tests, %4d (%6.2lf%%) correct classifications,\n",  
           TESTING_PATTERNS,  good_classifications,
	   100.0 * ((double)(good_classifications)) / ((double) TESTING_PATTERNS));
    printf("            %4d (%6.2lf%%) misclassifications\n", 
           misclassifications,
	   100.0 * ((double)(misclassifications)) / ((double) TESTING_PATTERNS));
}


/* mismatch returns true if any of the outputs of the network fail to
   match the desired outputs (within the tolerance DELTA).
*/

int mismatch(int p)
{
    int i, mismatch;

    mismatch = 0;
    for ( i = 1; i <= OPSIZE; i++ )  {
			if ( fabs(desired[p][i] - output[i]) > (DELTA * desired[p][i]) )  {
	    	mismatch = 1;
	    	break;
			}
    }
    return mismatch;
}

/* match returns true if all of the outputs of the network
   match the desired outputs (within the tolerance DELTA).
*/

int match(int p)
{
    int i, match;

    match = 0;
    for ( i = 1; i <= OPSIZE; i++ )  {
      if ( fabs(desired[p][i] - output[i]) < (DELTA * desired[p][i]) )  {
        match = 1;
      }
      else {
        match = 0;
        break;
      }
    }
    return match;
}


/* report_outputs displays the desired verses actual outputs of the 
   network for a particular input pattern.  Assumes that the current
   state of the network reflects the application of that input
   pattern (i.e., that a call to "forwardprop" with the pattern has
   occured before this routine is called).
*/

void report_outputs(int p, int type)
{
    int i;

    printf("Pattern %4d: ", p);
    if (  type == MISCLASS )         printf("M ");  /* "Hard" misclassification*/
    else if ( type == CLASS )        printf("C ");  /* Classification .........*/
    else if ( type == INDEF )        printf("I ");  /* Indefinite result.......*/
    else                             printf("? ");  /* Unknown code............*/
    
    for ( i = 1; i <= OPSIZE; i++ )  printf("%6.3f     ", desired[p][i]);
    printf("\n                ");
    for ( i = 1; i <= OPSIZE; i++ )  printf("%6.3f     ", output[i]);
    printf("\n");
}


/* Dump the weight matrices of the network in a "nice" form. */

void dump_weights(void)
{
    int i, j, k;

    printf("\n");
    for ( i = 0; i <= INSIZE; i++ )
        for ( j = 1; j <= HDSIZE; j++ )
          printf("w_ih[%1d][%1d] = %6.3f\n", i, j, w_ih[i][j]);
    printf("\n");
    for ( j = 0; j <= HDSIZE; j++ )
        for ( k = 1; k <= OPSIZE; k++ )
          printf("w_ho[%1d][%1d] = %6.3f\n", j, k, w_ho[j][k]);
    printf("\n");
}