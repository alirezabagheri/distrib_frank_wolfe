/*	TITLE: Distributed Frank Wolfe for SVM with Communication Drop
AUTHOR: Alireza Bagheri Garakani (me@alirezabagheri.com), Updated 1/23/2015

// Input data is in sparse format (see matlab function 'splitAtomMatrix.m').
// NOTE: first ROW of input is label.

*/

#include "frankwolfe_common.h"


class frankwolfe_svm_commdrop {

public:
	frankwolfe_svm_commdrop();
	bool setRandomSeed(int value);
	enum class variant_t { basic, linesearch, hardwork };
	bool setVariant(variant_t value);
	bool setEpsilon(float value);
	bool setMaxIterations(int value);
	bool setBeta(float value);
	bool setSVM_C(float value);
	bool setSVM_gamma(float value);
	bool setCommDropProb(float value);
	bool setSaveAtNumAtoms(set value);
	bool setSaveAtIter(set value);
	bool setSaveAll(bool value);
	bool loadTrainData(std::string inputfile_prefix);
	bool setDatasetName(std::string value);
	bool run();
	
private:
	// Variables
	variant_t _variant;
	float _epsilon, _R, _SVM_C, _SVM_gamma, _commDropProb;
	int _seed, _iterations, _trainAtoms, _trainAtomsLocal, _trainDimn, _mpi_numWorkers, _mpi_rank;
	set _saveAtNumAtoms, _saveAtIter;
	bool _saveAll;
	mat _trainA;
	vec _trainy;
	std::string _datasetName, _variant_str, _mpi_name;
	timer _timer;

	// Prepare msg_type structs
	typedef struct {
		float val;
		int rank;
	} msg_gradient;

	// Functions
	bool loadSparseData(mat &matrix_A, vec &vector_y, std::string inputfile);
	bool loadDenseData(mat &matrix_A, vec &vector_y, std::string inputfile);
	void log(const char *msg);
	void log(const std::string msg);

};
frankwolfe_svm_commdrop::frankwolfe_svm_commdrop(){
	// Set default values
	_variant = variant_t::basic;
	_epsilon = 0;
	_R = 0;
	_SVM_C = 1;
	_SVM_gamma = 1;
	_seed = 0;
	_commDropProb = 0;
	_iterations = 5000;
	_trainAtoms = 0;
	_trainAtomsLocal = 0;
	_trainDimn = 0;
	_datasetName = "unknown";
	_variant_str = "basic";
	_saveAll = false;
	_timer = time_init(); // Start timer


	// Get OpenMPI parameters
	MPI_Comm_size(MPI_COMM_WORLD, &_mpi_numWorkers);
	MPI_Comm_rank(MPI_COMM_WORLD, &_mpi_rank);
	char name[MPI_MAX_PROCESSOR_NAME]; int name_len;
	MPI_Get_processor_name(name, &name_len);
	_mpi_name = std::string(name);
}
bool frankwolfe_svm_commdrop::setRandomSeed(int value){
	_seed = value;
	return true;
	
}
bool frankwolfe_svm_commdrop::setVariant(variant_t value){
	_variant = value;

	switch (_variant){
	case variant_t::basic:
		_variant_str = "basic";
		break;
	case variant_t::linesearch:
		_variant_str = "linesearch";
		break;
	case variant_t::hardwork:
		_variant_str = "hardwork";
		break;
	}

	return true;
}
bool frankwolfe_svm_commdrop::setEpsilon(float value){
	_epsilon = value;
	return true;
}
bool frankwolfe_svm_commdrop::setMaxIterations(int value){
	if (value < 1) return false;
	_iterations = value;
	return true;
}
bool frankwolfe_svm_commdrop::setBeta(float value){
	_R = value;
	return true;
}
bool frankwolfe_svm_commdrop::setSVM_gamma(float value){
	_SVM_gamma = value;
	return true;
}
bool frankwolfe_svm_commdrop::setSVM_C(float value){
	_SVM_C = value;
	return true;
}
bool frankwolfe_svm_commdrop::setCommDropProb(float value){
	_commDropProb = value;
	return true;
}
bool frankwolfe_svm_commdrop::setSaveAtNumAtoms(set value){
	_saveAtNumAtoms.clear();
	_saveAtNumAtoms.insert(value.begin(), value.end());
	return true;
}
bool frankwolfe_svm_commdrop::setSaveAtIter(set value){
	_saveAtIter.clear();
	_saveAtIter.insert(value.begin(), value.end());
	return true;
}
bool frankwolfe_svm_commdrop::loadTrainData(std::string inputfile_prefix){
	// Format filename per node
	std::string inputfile_full = inputfile_prefix + ".of" + boost::lexical_cast<std::string>(_mpi_numWorkers)
		+ "." + boost::lexical_cast<std::string>(_mpi_rank + 1);
	// Read and update variables
	bool res = loadDenseData(_trainA, _trainy, inputfile_full);
	_trainDimn = _trainA.size1();
	_trainAtoms = _trainA.size2();
	_trainAtomsLocal = _trainAtoms;
	log("Finished loading training data.");
	return res;
}
bool frankwolfe_svm_commdrop::setDatasetName(std::string value){
	_datasetName = value;
	return true;
}
bool frankwolfe_svm_commdrop::setSaveAll(bool value){
	_saveAll = value;
	return true;
}


bool frankwolfe_svm_commdrop::loadSparseData(mat &matrix_A, vec &vector_y, std::string inputfile){
	typedef boost::tokenizer< boost::escaped_list_separator<char> > Tokenizer;
	
	// Read file
	std::ifstream in(inputfile.c_str());

	// Check if valid / ready to read
	if (!in.is_open()){
		log(std::string("ERROR: Could not open input file: ") + inputfile);
		return false;
	}

	log(std::string("Given input file: ") + inputfile);

	// Begin parsing
	std::string line;
	Tokenizer::iterator it;
	int row, col, elem = 0;
	float val;
	while (getline(in, line)) {
		// Tokenize line and set iterator to first item
		Tokenizer tokens(line);
		Tokenizer::iterator it = tokens.begin();
		
		// First item is row index (stating at 1)
		row = (int) std::strtof(it.current_token().c_str(),NULL);

		it++;

		// Second item is column index (stating at 1)
		col = (int) std::strtof(it.current_token().c_str(), NULL);

		it++;

		// Third item is value
		val = std::strtof(it.current_token().c_str(), NULL);

		// Add to matrix
		if (elem == 0){
			// value of 0 specifies matrix size information.
			// error thrown if not first entry in file.
			if (val != 0){
				log("ERROR: First line must specify matrix size (format: <row> <col> 0)");
				return false;
			}
			log(std::string("Reading atom matrix (") + boost::lexical_cast<std::string>(row-1)
				+ 'x' + boost::lexical_cast<std::string>(col) + ") and label vector ("
				+ boost::lexical_cast<std::string>(col)+")...");

			// Allocate matrix size
			matrix_A = mat(row - 1, col, 0);
			vector_y = vec(col, 0);
		} 
		else if (row == 1) vector_y(col - 1) = val; // First column denotes label
		else matrix_A(row - 2, col - 1) = val;

		elem++;
	}

	// Sanity check (all labels are +1/-1)
	for (size_t i = 0; i < vector_y.size(); i++){
		if (std::abs(vector_y(i)) != 1) {
			log(std::string("ERROR: all labels must be defined as +1/-1. Error for example: ")
				+ boost::lexical_cast<std::string>(i));
			return false;
		}
	}

	// Close file
	in.close();

	return true;
}

bool frankwolfe_svm_commdrop::loadDenseData(mat &matrix_A, vec &vector_y, std::string inputfile){
	typedef boost::tokenizer< boost::escaped_list_separator<char> > Tokenizer;

	// Read file
	std::ifstream in(inputfile.c_str());

	// Check if valid / ready to read
	if (!in.is_open()){
		log(std::string("ERROR: Could not open input file: ") + inputfile);
		return false;
	}

	log(std::string("Given input file: ") + inputfile);

	// Begin parsing
	std::string line;
	Tokenizer::iterator it;
	int row = 0;
	size_t size1 = 0, size2 = 0;
	while (getline(in, line)) {
		if ((size_t)row - 1 == size1){
			log("ERROR: Extra matrix rows found; invalid input file.");
			return false;
		}

		// Tokenize line and set iterator to first item
		Tokenizer tokens(line);
		Tokenizer::iterator it = tokens.begin();

		// First row is matrix size information
		if (row == 0){
			size1 = (size_t)std::strtof(it++.current_token().c_str(), NULL);
			size2 = (size_t)std::strtof(it++.current_token().c_str(), NULL);
			
			if (!it.at_end() || size1 < 1 || size2 < 1){
				log("ERROR: First line must specify valid matrix size with format: <row>,<col>,0,0,0,0,...");
				return false;
			}

			log(std::string("Reading atom matrix (") + boost::lexical_cast<std::string>(size1 - 1)
				+ 'x' + boost::lexical_cast<std::string>(size2)+") and label vector ("
				+ boost::lexical_cast<std::string>(size2)+")...");

			// Allocate matrix size
			matrix_A = mat(size1 - 1, size2, 0);
			vector_y = vec(size2, 0);
		}
		// Second row are labels
		else if (row == 1) {
			for (size_t col = 0; col < size2; col++){
				if (it.at_end()) {
					log("ERROR: Expected more matrix columns at row " + boost::lexical_cast<std::string>(row) 
						+ "; invalid input file.");
					return false;
				}
				vector_y(col) = std::strtof(it++.current_token().c_str(), NULL);
				if (std::abs(vector_y(col)) != 1) {
					log(std::string("ERROR: all labels must be defined as +1/-1. Error for example: ")
						+ boost::lexical_cast<std::string>(col));
					return false;
				}
			}
			if (!it.at_end()) {
				log("ERROR: Extra matrix columns at row " + boost::lexical_cast<std::string>(row) + "; invalid input file.");
				return false;
			}
		}
		// Remaining rows are atom matrix
		else {
			for (size_t col = 0; col < size2; col++){
				if (it.at_end()) {
					log("ERROR: Expected more matrix columns at row " + boost::lexical_cast<std::string>(row)
						+"; invalid input file.");
					return false;
				}
				matrix_A(row - 2, col) = std::strtof(it++.current_token().c_str(), NULL);
			}
			if (!it.at_end()) {
				log("ERROR: Extra matrix columns at row " + boost::lexical_cast<std::string>(row)+"; invalid input file.");
				return false;
			}
		}

		// Increment row
		row++;
	}
	if ((size_t)row - 1 != size1){
		log("ERROR: Expected more matrix rows, only found " + boost::lexical_cast<std::string>(row - 1) 
			+ "; invalid input file.");
		return false;
	}


	// Close file
	in.close();

	return true;
}

void frankwolfe_svm_commdrop::log(const char *msg){
	std::cout << "[" << time_now_str() << " (" << time_elapsed(_timer) << ") - "
		<< _mpi_name << " (rank " << _mpi_rank << ")]\t" << msg << std::endl;
}
void frankwolfe_svm_commdrop::log(const std::string msg){
	log(msg.c_str());
}



bool frankwolfe_svm_commdrop::run(){

	log("Preparing to run...");

	// Create output folder
	std::string directory_str("results/" + boost::lexical_cast<std::string>(_datasetName)
		+".of" + boost::lexical_cast<std::string>(_mpi_numWorkers)+"/");
	if (!boost::filesystem::exists("results/")) boost::filesystem::create_directory("results/");
	if (!boost::filesystem::exists(directory_str.c_str())) boost::filesystem::create_directory(directory_str.c_str());
	MPI_Barrier(MPI_COMM_WORLD);
	if (!boost::filesystem::exists(directory_str.c_str())){
		log("ERROR: Could not create output directory; will not continue.");
		return false;
	}

	// Construct output filename based on parameters
	std::stringstream outputFilename_ss;
	outputFilename_ss << directory_str << "dfw.svm.commdrop_dataset." << _datasetName
		<< "_variant." << _variant_str << "_R." << _R
		<< "_epsilon." << _epsilon << "_Seed." << _seed
		<< "_gamma." << _SVM_gamma << "_C." << _SVM_C
		<< "_dropProb." << _commDropProb
		<< "_node." << (_mpi_rank + 1) << "of" << _mpi_numWorkers;

	// Check if file already exists
	if (boost::filesystem::exists(outputFilename_ss.str())){
		log("ERROR: Output file already exists; will not continue.");
		return false;
	}

	// Open file pointer
	std::ofstream output(outputFilename_ss.str());
	if (!output.is_open()){
		log("ERROR: Cannot open output file pointer.");
		return false;
	}

	// Prepare for running algorithm
	using namespace boost::numeric::ublas;

	// One atom must be shared with all nodes for initialization
	int num_nonzero = 1, comm_cost = 0;
	float *atom = (float *)malloc((_trainDimn + 1)* sizeof(float));
	bimap atom_index_lookup;

	// Each node should update bimap
	int local_index = _mpi_rank == 0 ? _seed % _trainAtoms : _trainAtoms;
	atom_index_lookup.insert(bimap::value_type(local_index, -1));

	// Sender should prepare data to send
	if (_mpi_rank == 0){
		log(std::string("Sending local atom ") + boost::lexical_cast<std::string>(local_index) + " for initialization...");
		for (int i = 0; i < _trainDimn; i++){
			atom[i] = _trainA(i, local_index);
			if (atom[i] != 0) comm_cost++;
		}
		atom[_trainDimn] = _trainy(local_index);
	}

	// Broadcast atom
	MPI_Bcast(atom, _trainDimn + 1, MPI_FLOAT, 0, MPI_COMM_WORLD);

	// Receivers should append atom as a new column to trainA.
	if (_mpi_rank != 0){
		// Update atom matrix
		_trainAtoms++;
		_trainA.resize(_trainDimn, _trainAtoms);
		for (int i = 0; i < _trainDimn; i++) {
			_trainA(i, local_index) = atom[i];
			if (atom[i] != 0) comm_cost++;
		}
		// Update y vector
		_trainy.resize(_trainAtoms);
		_trainy(_trainAtoms - 1) = atom[_trainDimn];
	}

	// Initialize alpha to valid point on simplex
	vec alpha(_trainAtoms, 0);
	alpha(local_index) = 1;

	// Precompute some useful quantities
	vec ones_d(_trainDimn, 1); // todo: check if built-in
	vec R(_trainAtoms); noalias(R) = prod(ones_d, element_prod(_trainA, _trainA));

	// Compute initial K
	mat_col _trainA_j(_trainA, local_index);
	vec ones_n(_trainAtoms, 1); // todo: check if built-in
	vec K(_trainAtoms);  noalias(K) = R + (ones_n * inner_prod(_trainA_j, _trainA_j)) - (2 * prod(_trainA_j, _trainA));
	for (size_t i = 0; i < K.size(); i++) K(i) = _trainy(local_index) * _trainy(i) * (exp(-_SVM_gamma * K(i)) + 1);
	K(local_index) += 1 / _SVM_C;

	// Prepare for loop
	vec gradient(_trainAtoms);
	set saveAtIter(_saveAtIter), saveAtNumAtoms(_saveAtNumAtoms);
	float gamma = 1, objective = 0, duality_gap = 0; // objective is incrementally updated
	bool dropSend = false, dropRecv = false;
	int offset = 0;
	timer iteration_time;
	
	log("Running algorithm...");
	for (int iteration = 0; iteration < _iterations; iteration++){

		// Reset timer
		iteration_time = time_init();

		// Calculate gradient
		if (!dropRecv){
			if (iteration == 0) noalias(gradient) = 2 * K;
			else gradient = ((1 - gamma) * gradient) + (2 * gamma * K);
		}

		// Choose atom corresponding to smallest entry of the LOCAL gradient
		local_index = 0;
		float value, local_min = gradient(0);
		for (size_t e = 1; e < (size_t)_trainAtomsLocal; e++){
			value = gradient(e);
			if (value < local_min){
				// Update min value and index
				local_min = value;
				local_index = e;
			}
		}

		// Determine if node should "drop" sending this gradient max
		dropSend = random01() < _commDropProb;
		if (dropSend) local_min = std::numeric_limits<float>::max();

		// Broadcast and reduce (min) to determine smallest global gradient and corresponding sender
		msg_gradient local_gradient = { local_min, _mpi_rank }, global_gradient;
		MPI_Allreduce(&local_gradient, &global_gradient, 1, MPI_FLOAT_INT, MPI_MINLOC, MPI_COMM_WORLD);

		// Synchronize index of smallest gradient (global_index)
		int global_index;
		if (global_gradient.rank == _mpi_rank){
			// Sender should determine index to send. If index (with corresponding atom) was
			// sent before at iteration j, then broadcast j. Otherwise, broadcast -1.
			if (atom_index_lookup.left.count(local_index) == 1) {
				global_index = atom_index_lookup.left.at(local_index);
			} else {
				global_index = -1;
				atom_index_lookup.insert(bimap::value_type(local_index, iteration));
			}
		}

		// Broadcast index of smallest gradient
		MPI_Bcast(&global_index, 1, MPI_INT, global_gradient.rank, MPI_COMM_WORLD);

		if (global_gradient.rank != _mpi_rank){
			// Receivers should determine their respective local_index and update map
			if (global_index != -1) {
				local_index = atom_index_lookup.right.at(global_index);
			} else {
				// Expecting a new atom to be broadcasted; set local_index to end+1 of
				// current atom matrix. Also, update map.
				local_index = _trainAtoms;
				atom_index_lookup.insert(bimap::value_type(local_index, iteration));
			}
		}

		// If atom needs to be sent, do it now.
		if (global_index == -1){
			num_nonzero++; 

			// Sender should prepare data to send
			if (global_gradient.rank == _mpi_rank){
				log(std::string("Sending local atom ") + boost::lexical_cast<std::string>(local_index) 
					+ " on iteration " + boost::lexical_cast<std::string>(iteration) + "...");
				for (int i = 0; i < _trainDimn; i++){
					atom[i] = _trainA(i, local_index);
					if (atom[i] != 0) comm_cost++;
				}
				atom[_trainDimn] = _trainy(local_index);
			}

			// Broadcast atom
			MPI_Bcast(atom, _trainDimn + 1, MPI_FLOAT, global_gradient.rank, MPI_COMM_WORLD);

			// Receivers should append atom as a new column to trainA.
			if (global_gradient.rank != _mpi_rank){
				_trainAtoms++;
				// Update atom 
				_trainA.resize(_trainDimn, _trainAtoms);
				for (int i = 0; i < _trainDimn; i++) {
					_trainA(i, local_index) = atom[i];
					if (atom[i] != 0) comm_cost++;
				}
				// Update y vector
				_trainy.resize(_trainAtoms);
				_trainy(_trainAtoms - 1) = atom[_trainDimn];
				// Update alpha
				alpha.resize(_trainAtoms);
				alpha(_trainAtoms - 1) = 0;
				// Update gradient
				gradient.resize(_trainAtoms);
				gradient(_trainAtoms - 1) = 0;
				// Update R vector
				R.resize(_trainAtoms);
				mat_col _trainA_j(_trainA, local_index);
				R(_trainAtoms - 1) = inner_prod(_trainA_j, _trainA_j);
				// Resize K vector
				K.resize(_trainAtoms);
			}
		}

		// Update K vector
		mat_col _trainA_j(_trainA, local_index);
		vec ones_n(_trainAtoms, 1); // todo: check if built-in
		noalias(K) = R + (ones_n * inner_prod(_trainA_j, _trainA_j)) - (2 * prod(_trainA_j, _trainA));
		for (size_t i = 0; i < K.size(); i++) K(i) = _trainy(local_index) * _trainy(i) * (exp(-_SVM_gamma * K(i)) + 1);
		K(local_index) += 1 / _SVM_C;

		// Determine if node should "drop" receiving this atom
		dropRecv = (global_gradient.rank != _mpi_rank) && (random01() < _commDropProb);
		if (dropRecv) {
			offset++;
		} else {
			// Define gamma for current iteration
			gamma = 2.0f / (iteration - offset + 2);

			// Define objective and duality gap
			objective = (pow(1 - gamma, 2) * objective) + (pow(gamma, 2) * K(local_index)) + (2 * gamma * (1 - gamma) * inner_prod(alpha, K));

			// If no "drop", perform alpha update
			switch (_variant) {
			case variant_t::basic:
				alpha *= (1 - gamma);
				alpha(local_index) += gamma;

				// On first iteration, initial alpha is cleared.
				if (iteration == 0) num_nonzero--;
				break;
			default:
				log("ERROR: Unsupported variant.");
				break;
			}
		}

		// Root node should measure statistics (If too much here, then node may become straggler)
		if (_saveAll || saveAtIter.erase(iteration) + saveAtNumAtoms.erase(num_nonzero) > 0){
			float elapsed = time_elapsed(iteration_time);

			// Write all metrics to disk
			output << (iteration+1) << ',' << elapsed << ',' << num_nonzero << ','
				<< comm_cost << ',' << ((dropSend ? 1 : 0) + (dropRecv ? 2 : 0)) 
				<< ',' << objective << '\n';
		}
		
		// Print progress
		//if (iteration % 1000 == 0 && iteration != 0) log("Completed iteration " + iteration);
	}

	// Close file pointer
	output.close();

	log("Done.");

	return true;	
}

int main(int argc, char *argv[]) {

	// Initialize openMPI
	MPI_Init(&argc, &argv);

	// If debug is enabled, allow client to use gdb to attach.
	#ifdef DEBUG_ENABLED
	int i = 0;
	char hostname[256];
	gethostname(hostname, sizeof(hostname));
	printf("PID %d on %s ready for attach\n", getpid(), hostname);
	fflush(stdout);
	while (0 == i)
		sleep(5);
	#endif


	// Read arguments
	if (argc != 9){
		int rank = 0;
		MPI_Comm_rank(MPI_COMM_WORLD, &rank);

		if (rank == 0){
			printf("Usage: %s <str:dataset_name> <path:data> <float:epsilon> <float:beta> "
				"<size:max_iterations> <float:SVM_C> <float:SVM_gamma> <float:comm_drop_prob>\n\n", argv[0]);
			printf("\t\t dataset_name - used as name for result directory\n");
			printf("\t\t data - path prefix to data (e.g., worker 3 of 10 will append '.of10.3' to path)\n");
			printf("\t\t epsilon - optimization error (duality gap) used for stopping criteria\n");
			printf("\t\t beta - sparsity constraint\n");
			printf("\t\t max_iterations - indicates max number of iterations to run\n");
			printf("\t\t SVM_C - regularization parameter for SVM\n");
			printf("\t\t SVM_gamma - RBF kernel parameter for SVM: exp(-gamma*|u-v|^2)\n");
			printf("\t\t comm_drop_prob - probability of dropping commnication per iteration with any neighbor node\n");
		}
		// Terminate
		MPI_Finalize();
		return 1;
	}


	const char * dataset_name = argv[1];
	const char * dataset_path = argv[2];
	const float epsilon = atof(argv[3]);
	const float beta = atof(argv[4]);
	const int max_iterations = atoi(argv[5]);
	const float svm_C = atof(argv[6]);
	const float svm_gamma = atof(argv[7]);
	const float comm_drop_prob = atof(argv[8]);


	// Prepare dFW solver
	frankwolfe_svm_commdrop fw;
	fw.setDatasetName(dataset_name);
	if (!fw.loadTrainData(dataset_path)) return 2;
	fw.setEpsilon(epsilon);  // unreachable stoppping criterion
	fw.setBeta(beta);
	fw.setMaxIterations(max_iterations); // run for a long time.
	fw.setSVM_C(svm_C);
	fw.setSVM_gamma(svm_gamma);
	fw.setCommDropProb(comm_drop_prob);


	// Variants include basic, line-search, and hard-working.
	// For now, always use basic (see gamma in paper)
	fw.setVariant(frankwolfe_svm_commdrop::variant_t::basic);


	// Indicates frequency of reporting statistics to log file
	// Currently "save all" flag is set to generate report on every iteration.
	// Other options exists: see setSaveAtNumAtoms() or setSaveAtIter().
	fw.setSaveAll(true);
	//set saveAtNumAtoms = { 2, 4, 6, 8 };
	//fw.setSaveAtNumAtoms(saveAtNumAtoms);
	//set saveAtIter = {0,1,2,3,4,5,6,7,8,9,10};
	//fw.setSaveAtIter(saveAtIter);
	
	
	// Run solver
	fw.run();


	// Terminate
	MPI_Finalize();
	return 0;
}
