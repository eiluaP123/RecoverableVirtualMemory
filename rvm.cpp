#include "rvm.h"
#include <list>
#include <vector>
#include <algorithm>

using namespace std;
///map<const char*, void*> map_segname_segbase; //segname, segbase
map<const char*, struct segment> map_segname_segment;
map<void*, const char*> map_segbase_segname; //segbase, segname
list<segment> list_of_mem_segments;
map<trans_t, list<char *> > tid_list_segments;
//map<trans_t, list<void *>) tid_list_segments_modified;
map<trans_t, rvm_t> tid_rvm;
map<trans_t, vector<struct changes *> > tid_changes;
map<char *, void *> map_segname_undo;
trans_t txn_id = 0;

/** Initialize the library with the specified directory as backing store. **/
rvm_t rvm_init(const char *directory){
	rvm_t rvm;
	rvm.dir = (char *) directory;
	char* command = (char *) malloc(strlen(directory) + 6);
	strcpy(command,"mkdir ");
	strcat(command, directory);
	system(command);
	//printf("%s Directory Created\n", directory);
	return rvm;
}

/** map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice. **/
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
	char path[50];
	strcpy (path, rvm.dir);
	strcat (path,"/");
	strcat (path, segname);
	struct stat status_segment;

	//check if the folder is present, the segment does not exist
	if(stat(path, &status_segment) < 0)
	{
		//segment does not exist
		//Create the segment on disk
		char tmp[50];
		strcpy (tmp, "mkdir ");
		strcat (tmp,path);
		system(tmp);
		//printf("segment created on disk\n");

		//VM segment creation as per size passed to the function.
		segment *new_segment = (segment *) malloc(size_to_create);//has space for data also
		new_segment->address = new_segment;
		new_segment->seg_name = segname;
		new_segment->size = size_to_create;
		new_segment->isBusy = 0;
		new_segment->isMapped = 1;

		//create the text file
		strcat(path, "/");
		strcat(path, segname);
		strcat(path, ".txt");
		ofstream mfile (path);

		//add the created segment to the map. 
		map_segname_segment.insert(pair<const char*, struct segment> (segname, *new_segment));
		map_segbase_segname.insert(pair<void *, const char *>(new_segment,segname));
		map<void *, const char *>::iterator it;
		// for(it = map_segbase_segname.begin(); it != map_segbase_segname.end() ; it ++){
		// 	cout << "print1 " << it->first << endl;
		// 	cout << "print value 1 " << it->second << endl;
		// }
		return (void *) new_segment->address;
	}

	//the segment exisits on the disk
	else
	{	
		//get the size of the file
		strcat(path, "/");
		strcat(path, segname);
		strcat(path, ".txt");
		int fd = open(path, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG);
	    assert(fd != -1 && "log file could not be created\n");
	    int fsize = lseek(fd, 0, SEEK_END);

	    if(fsize<size_to_create)
	    {	//copy contents from a file and then expand size
	    	void *n_segment = malloc(fsize);
	    	char *line;
	    	lseek(fd, 0, SEEK_SET);
        	int success = read(fd, n_segment, fsize);
        	assert(success == fsize && "could not read\n");
        	n_segment = realloc(n_segment, size_to_create);
        	map_segbase_segname.insert(pair<void *, const char *>(n_segment,segname));
        	map_segname_segment.insert(pair<const char*, struct segment> (segname, *( struct segment*)n_segment));
        	close(fd);
			return n_segment;
		}
		else
		{
			void *n_segment = malloc(size_to_create);
			lseek(fd, 0, SEEK_SET);
        	int success = read(fd, n_segment, size_to_create);
        	map_segbase_segname.insert(pair<void *, const char *>(n_segment,segname));
        	map_segname_segment.insert(pair<const char*, struct segment> (segname, *(struct segment*)n_segment));
        	close(fd);

        	return n_segment;

		}
		//map_segname_segment.insert(pair<const char*, struct segment> (segname, *n_segment));
		

	}
}

/** unmap a segment from memory **/
void rvm_unmap(rvm_t rvm, void *segbase){
	segment *find_segment = (segment *) segbase;
	find_segment->isMapped = 0;
	find_segment->address = NULL;
	char *name = map_segbase_segname[segbase];
	map_segbase_segname.erase(segbase);
	map_segname_segment.erase(name);
}

/**destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped.**/
void rvm_destroy(rvm_t rvm, const char *segname){
	map<const char *, segment>::iterator it = map_segname_segment.find(segname);
	if(it!= map_segname_segment.end())
	{
		segment existing_segment = map_segname_segment[segname];
		segment *ptr = &existing_segment;
		if(existing_segment.isMapped == 0)
		{
			//free(ptr);
			char tmp[50];
			strcpy(tmp,"rm -rf ");
			strcat(tmp, rvm.dir);
			strcat (tmp,"/");
			strcat (tmp, segname);
			system(tmp);
		}
		else
		{
			printf("Segment currenty mapped. Cannot destroy\n");
		}
	}
	else
	{
		printf("Segment does not exist. Cannot destroy!\n");
	}
}

/** begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trant_t needs to be able to be typecasted to an integer type. **/
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases)
{
	list <char *> l;
	txn_id ++;
	for(int i=0; i<numsegs; i++){
		segment *addr = segbases[i];

		char *name = map_segbase_segname[addr];
		segment seg = map_segname_segment[name];
		cout << "name " << name << endl;
		void *undo_seg = malloc(seg.size);
		//cout << "seg.address " << (char *) seg.address << endl;
		//cout << "unod seg.address " << (char *) undo_seg << endl;
		memcpy(undo_seg, seg.address, seg.size);


		// FILE *f=fopen("undo1.txt", "w");
		// fseek(f, 0, SEEK_SET);
		// fwrite((char*)undo_seg, seg.size, 1, f);
		
		//cout << "undo seg.address 1" << (char *) undo_seg << endl;
		map_segname_undo.insert(pair<char *, void*> (name, undo_seg));
		//void * check = map_segname_undo[name];

		// FILE *f1=fopen("map.txt", "w");
		// fseek(f1, 0, SEEK_SET);
		// fwrite((char*)check, seg.size, 1, f);

		l.push_back(name);
		if(seg.isBusy != 0)
		{
			//cout << "A segment already in use\n";
			return (trans_t) -1;
		}
	}
	tid_list_segments.insert(pair<trans_t, list<char *> > (txn_id, l));
	tid_rvm.insert(pair<trans_t, rvm_t>(txn_id, rvm));

	//cout << "map tid_list_segments size " << tid_list_segments.size() << endl;
	//printf("No segments wanted currently in use. go ahead.\n");
	return (trans_t) 1;
}

/**declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area.**/
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)
{
	list<char *> list_t_segs = tid_list_segments[tid];
	//cout<<"size of list="<<list_t_segs.size();
	map<void *, const char *>::iterator it;
	// for(it = map_segbase_segname.begin(); it != map_segbase_segname.end() ; it ++){
	// }
	vector<struct changes *> list_undo;
	const char *name = map_segbase_segname[segbase];
	list<char *>::iterator iter;
	// for(iter = list_t_segs.begin(); iter != list_t_segs.end() ; iter ++){
	// }
	bool found = find(list_t_segs.begin(),list_t_segs.end(),name) != list_t_segs.end();
	if(found)
	//The segment belongs to the list of segments specified in begin_trans
	{
		cout << "enter" << endl;
		changes *undo = malloc(sizeof(struct changes));
		undo->start = segbase;
		undo->offset = offset;
		undo->size = size;
		undo->data = (char *)malloc(size); 
		//memcpy(undo->data, (char *) segbase + offset, size);
		memcpy(undo->data, (char *) segbase, size);
		map<trans_t, vector<struct changes*> >::const_iterator itr = tid_changes.find(tid);
		//cout<<"undo data ="<<undo->data;
		if(itr != tid_changes.end()){
			list_undo = tid_changes[tid];
			list_undo.push_back(undo);
			tid_changes[tid] = list_undo;
		}
		else
		{
			list_undo.push_back(undo);
			tid_changes[tid] = list_undo;
		}
	}
	else
	{
		printf("Trying to modify segment not in the list for this transaction\n");
	}
}

/** commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts. **/
void rvm_commit_trans(trans_t tid){
	vector<struct changes*> list_changes_for_tid = tid_changes[tid];
	cout<<"Vector size= "<<list_changes_for_tid.size();
	for(vector<struct changes*>::iterator iter = list_changes_for_tid.begin(), end = list_changes_for_tid.end(); iter != end; ++iter)
	{	
		//char *text = (*iter)->start + (*iter)->offset;

		char *text = (*iter)->start;
		//cout<<"text ="<<text;

		char*  mod_segname = map_segbase_segname[(*iter)->start];
		map<trans_t, rvm_t>::iterator itr= tid_rvm.find(tid);
		if(itr != tid_rvm.end()){
		char path[100];
		strcpy(path, itr->second.dir);
		strcat (path,"/");
		strcat (path, mod_segname);
		strcat (path,"/");
		strcat (path, mod_segname);
		strcat (path, ".txt");

		FILE *f=fopen(path, "w");
		fseek(f, (*iter)->offset, SEEK_SET);
		fwrite(text, (*iter)->size, 1, f);
		
		// ofstream file(path, ios::out | ios::app);
		// //file << (*iter)->offset << ':' << text << '\n';
		// file << text;

		// file.close();
		}
	}
	list<char *> list_segs = tid_list_segments[tid];
	for(list<char *>::iterator iter = list_segs.begin(), end=list_segs.end(); iter != end; iter ++){
		free(map_segname_undo[(*iter)]);
	}
	map_segname_undo.clear();
}

/** undo all changes that have happened within the specified transaction. **/
void rvm_abort_trans(trans_t tid){
	list<char *> list_segs_tid = tid_list_segments[tid];
	for(list<char *>::iterator iter = list_segs_tid.begin(), end=list_segs_tid.end(); iter != end; iter++){
		char * name = *iter;
		void *undo_in_mem = map_segname_undo[name];

		segment seg = map_segname_segment[name];
		void *address = seg.address;
		memcpy(address, undo_in_mem, seg.size);

		free(undo_in_mem);
	}

}

/** play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible. **/
void rvm_truncate_log(rvm_t rvm){
// 	char tmp[50];
// 	char command[50];
// 	strcat(command, "rm ");
	
// 	//strcat(tmp, rvm.dir);


// 	// strcat (tmp, segname);
// 	// system(tmp);


// DIR *dir;
// struct dirent *ent;
// if ((dir = opendir (rvm.dir)) != NULL) {
//   /* print all the files and directories within directory */
//   while ((ent = readdir (dir)) != NULL) {
//     printf ("%s\n", ent->d_name);
//     strcpy(tmp, );
//     strcat(tmp, "/");
//     strcat (tmp, ent->d_name);
//     strcat (tmp, ".txt");
//     ofstream myfile(tmp);

//     if(!myfile.is_open()){
//     	strcat(command, tmp);
//     	system(command);

//     }
//   }
//   closedir (dir);
// } else {
//   /* could not open directory */
//   perror ("");
//   return EXIT_FAILURE;
// }

	
}