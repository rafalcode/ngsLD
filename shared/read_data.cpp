
#include "read_data.hpp"

/*
Function to read both called genotypes (1 field per site and indiv), genotype lkls or genotype post probs (3 fields per site and indiv)
  in_geno           : file name to read from
  in_bin            : is input file in binary?
  in_probs          : is input GLs or genot posterior probs?
  in_logscale       : are the probs in log scale? (is updated to true at the end because function always returns genotypes in logscale)
  n_ind             : number of individuals
  n_sites           : number of sites
*/
double*** read_geno(char *in_geno, bool in_bin, bool in_probs, bool *in_logscale, uint64_t n_ind, uint64_t n_sites){
  uint64_t n_fields;
  // Depending on input we will have either 1 or 3 genot
  uint64_t n_geno = (in_probs ? N_GENO : 1);
  double *t, *ptr;
  char *buf = init_ptr(BUFF_LEN, (const char*) '\0');

  // Allocate memory
  double ***geno = init_ptr(n_ind, n_sites+1, N_GENO, -INF);
  
  // Open GENO file
  gzFile in_geno_fh = open_gzfile(in_geno, in_bin ? "rb" : "r");
  if(in_geno_fh == NULL)
    error(__FUNCTION__, "cannot open GENO file!");

  for(uint64_t s = 1; s <= n_sites; s++){
    if(in_bin){
      for(uint64_t i = 0; i < n_ind; i++){
	if( gzread(in_geno_fh, geno[i][s], N_GENO * sizeof(double)) != N_GENO * sizeof(double) )
	  error(__FUNCTION__, "cannot read binary GENO file. Check GENO file and number of sites!");
	if(!*in_logscale)
	  conv_space(geno[i][s], N_GENO, log);
	// Normalize GL
	post_prob(geno[i][s], geno[i][s], NULL, N_GENO);
	// Check if OK
        if(isnan(geno[i][s][0]) ||
           isnan(geno[i][s][1]) ||
           isnan(geno[i][s][2]))
          error(__FUNCTION__, "NaN found! Is the file format correct?");
      }
    }
    else{
      if( gzgets(in_geno_fh, buf, BUFF_LEN) == NULL)
	error(__FUNCTION__, "cannot read GZip GENO file. Check GENO file and number of sites!");
      // Remove trailing newline
      chomp(buf);
      // Check if empty
      if(strlen(buf) == 0)
	continue;
      // Parse input line into array
      n_fields = split(buf, (const char*) " \t", &t);

      // Check if header and skip
      if(!n_fields){
	fprintf(stderr, "> Header found! Skipping line...\n");
        if(s != 1){
          warn(__FUNCTION__, " header found but not on first line. Is this an error?");
          fprintf(stderr, "%s/n", buf);
        }
        s--;
	continue;
      }

      if(n_fields < n_ind * n_geno)
	error(__FUNCTION__, "wrong GENO file format. Less fields than expected!");
      
      // Use last "n_ind * n_geno" columns
      ptr = t + (n_fields - n_ind * n_geno);
      
      for(uint64_t i = 0; i < n_ind; i++){
	if(in_probs){
          for(uint64_t g = 0; g < N_GENO; g++)
            geno[i][s][g] = (*in_logscale ? ptr[i*N_GENO+g] : log(ptr[i*N_GENO+g]));
	}else{
	  int g = (int) ptr[i];
	  if(g >= 0){
	    if(g > 2)
	      error(__FUNCTION__, "wrong GENO file format. Genotypes must be coded as {-1,0,1,2} !");
	    geno[i][s][g] = log(1);
	  }else
	    geno[i][s][0] = geno[i][s][1] = geno[i][s][2] = log((double) 1/N_GENO);
        }
	
	// Normalize GL
	post_prob(geno[i][s], geno[i][s], NULL, N_GENO);
      }

      // Free memory
      delete [] t;
    }
  }

  // Check if file is at EOF
  gzread(in_geno_fh, buf, 1);
  if(!gzeof(in_geno_fh))
    error(__FUNCTION__, "GENO file not at EOF. Check GENO file and number of sites!");

  gzclose(in_geno_fh);
  delete [] buf;
  // Read_geno always returns genos in logscale
  *in_logscale = true;
  return geno;
}







double* read_pos(char *in_pos, uint64_t n_sites){
  uint64_t n_fields;
  char *buf = init_ptr(BUFF_LEN, (const char*) '\0');

  char *prev_chr = init_ptr(BUFF_LEN, (const char*) '\0');
  uint64_t prev_pos = 0;

  // Allocate memory
  double *pos_dist = init_ptr(n_sites+1, (double) INFINITY);

  // Open file
  gzFile in_pos_fh = open_gzfile(in_pos, "r");
  if(in_pos_fh == NULL)
    error(__FUNCTION__, "cannot open POS file!");

  for(uint64_t s = 1; s <= n_sites; s++){
    char **tmp;
    if( gzgets(in_pos_fh, buf, BUFF_LEN) == NULL)
      error(__FUNCTION__, "cannot read next site from POS file!");
    // Remove trailing newline
    chomp(buf);
    // Check if empty
    if(strlen(buf) == 0)
      continue;
    // Parse input line into array
    n_fields = split(buf, (const char*) " \t", &tmp);

    // Check if header and skip
    if(!n_fields || strtod(tmp[1], NULL)==0){
      fprintf(stderr, "> Header found! Skipping line...\n");
      if(s != 1){
	warn(__FUNCTION__, " header found but not on first line. Is this an error?");
	fprintf(stderr, "%s/n", buf);
      }
      s--;
      continue;
    }

    if(n_fields < 2)
      error(__FUNCTION__, "wrong POS file format!");

    // If first chr to be parsed
    if(strlen(prev_chr) == 0)
      strcpy(prev_chr, tmp[0]);
    
    if(strcmp(prev_chr, tmp[0]) == 0){
      pos_dist[s] = strtod(tmp[1], NULL) - prev_pos;
      if(pos_dist[s] < 1)
	error(__FUNCTION__, "invalid distance between adjacent sites!");
    }else{
      pos_dist[s] = INFINITY;
      strcpy(prev_chr, tmp[0]);
    }
    prev_pos = strtoul(tmp[1], NULL, 0);

    // Free memory
    free_ptr((void**) tmp, n_fields);
  }

  // Check if file is at EOF
  gzread(in_pos_fh, buf, 1);
  if(!gzeof(in_pos_fh))
    error(__FUNCTION__, "POS file not at EOF. Check POS file and number of sites!");

  gzclose(in_pos_fh);
  delete [] buf;
  delete [] prev_chr;

  return pos_dist;
}
