/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */
#include <inttypes.h>

#include <doca_apsh.h>
#include <doca_log.h>
#include <map>
#include <unistd.h>

//c++
#include <vector>
#include <memory>
#include <fstream>
#include <set>
#include <iostream>

#include "apsh_common.h"
#include "common.h"
#include <time.h>

DOCA_LOG_REGISTER(PSLIST);

/*
 * Calls the DOCA APSH API function that matches this sample name and prints the result
 *
 * @dma_device_name [in]: IBDEV Name of the device to use for DMA
 * @pci_vuid [in]: VUID of the device exposed to the target system
 * @os_type [in]: Indicates the OS type of the target system
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */

struct Node {
	unsigned int pid;
	unsigned int euid;
	std::set<unsigned int> child_pids;
	unsigned int ppid;
};
std::map<unsigned int, struct Node> tree; // {pid:node}

void printTree() {
	// 出力ファイルを開く
    std::ofstream file("tree_output.txt");
    if (!file.is_open()) {
		DOCA_LOG_ERR("File couldn't open");
    }
	for(const auto &pair : tree){
		unsigned int pid = pair.first;
		file << "PID: " << pid << ", EUID: " << tree[pid].euid << ", PPID: " << tree[pid].ppid << std::endl;
	}
	file << std::endl;
	file.close();
}
void printProcess(struct doca_apsh_process **pslist, int num_processes){
	std::ofstream file("dma_output.txt");
    if (!file.is_open()) {
		DOCA_LOG_ERR("File couldn't open");
    }
	/* Print some attributes of the processes */
	for (int i = 0; i < num_processes; ++i) {
		//unsigned user_id = doca_apsh_process_info_get(pslist[i],DOCA_APSH_PROCESS_LINUX_GID);
		//char *cmd = doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_COMM);
		file << "\tProcess " << i
     << " - name: " << doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_COMM)
     << ", pid: " << doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_PID)
     << ", uid: " << doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_LINUX_UID)
     << ", gid: " << doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_LINUX_GID)
     << ", ppid: " << doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_PPID)
     << std::endl;
	}
	file.close();
}

void sig_stop_process(unsigned int pid){
	DOCA_LOG_INFO("priviledge escalation detected in pid %d\n",pid);
}

int validate(unsigned int current_pid, unsigned int euid){
	int detected_count = 0;
	for(const unsigned int& next_pid : tree[current_pid].child_pids){
		detected_count = validate(next_pid, euid);
	}
	if(tree[current_pid].euid != euid){
		sig_stop_process(current_pid);
		return detected_count + 1;
	}
	return detected_count;
}


doca_error_t
pslist(const char *dma_device_name, const char *pci_addr, const char *pci_vuid, enum doca_apsh_system_os os_type)
{
	doca_error_t result;
	int num_processes, i;
	struct doca_apsh_ctx *apsh_ctx;
	struct doca_apsh_system *sys;
	struct doca_apsh_process **pslist;
	/* Hardcoded pathes to the files created by doca_apsh_config tool */
	const char *os_symbols = "/home/ubuntu/conf/symbols.json";
	const char *mem_region = "/home/ubuntu/conf/mem_regions.json";

	/* Init */
	result = init_doca_apsh(dma_device_name, &apsh_ctx);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init the DOCA APSH lib");
		return result;
	}
	DOCA_LOG_INFO("DOCA APSH lib context init successful");

	result = init_doca_apsh_system(apsh_ctx, os_type, os_symbols, mem_region, pci_vuid, &sys);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init the system context");
		return result;
	}
	DOCA_LOG_INFO("DOCA APSH system context created");


	// result = doca_apsh_processes_get(sys, &pslist, &num_processes);
	// if (result != DOCA_SUCCESS) {
	// 	DOCA_LOG_ERR("Failed to create the process list");
	// 	cleanup_doca_apsh(apsh_ctx, sys);
	// 	return result;
	// }
	// DOCA_LOG_INFO("Successfully performed %s. Host system contains %d processes", __func__, num_processes);

	#ifdef DEBUG
    clock_t start, end;
    double cpu_time_used;

    start = clock();  // 開始時刻の記録
	#endif






	std::map<unsigned int,unsigned int> euid_of_stepd; // {stepd pid: euid}
	unsigned int pid = 0;
	unsigned int euid = 0;
	unsigned int ppid = 0;
	while(1){
		result = doca_apsh_processes_get(sys, &pslist, &num_processes);
        	if (result != DOCA_SUCCESS) {
                	DOCA_LOG_ERR("Failed to create the process list");
                	cleanup_doca_apsh(apsh_ctx, sys);
        	        return result;
	        }

		for (i = 0; i < num_processes; ++i) {
			pid = doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_PID);
			euid = doca_apsh_process_info_get(pslist[i],DOCA_APSH_PROCESS_LINUX_UID);
			ppid = doca_apsh_process_info_get(pslist[i],DOCA_APSH_PROCESS_PPID);

			if (ppid < 3)continue;
			if (tree.find(pid) == tree.end()) {
				struct Node node;
				node.pid = pid;
				node.ppid = 0;
				node.euid = 0;
				tree[pid] = node;
			}
			if (tree.find(ppid) == tree.end()){
				struct Node parent_node;
				parent_node.pid = ppid;
				parent_node.child_pids.insert(pid);
				parent_node.ppid = 0;
				parent_node.euid = 9999;
				tree[ppid] = parent_node;
			}
			unsigned int prev_ppid = tree[pid].ppid;
			if (prev_ppid == ppid)continue;
			if (prev_ppid != 0) tree[prev_ppid].child_pids.erase(pid);
			tree[pid].ppid = ppid;
			tree[pid].euid = euid;
		}

		//printTree();

		// test for each stepd
		for(std::map<unsigned int, unsigned int>::iterator it = euid_of_stepd.begin(); it != euid_of_stepd.end(); ++it){
			pid = it->first;
			euid = it->second;
			tree[pid].euid = euid;
			validate(pid,euid);
		}

		/* Print some attributes of the processes */
		//printProcess(pslist,num_processes);

		doca_apsh_processes_free(pslist);

		#ifdef DEBUG
    	end = clock();  // 終了時刻の記録
    	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;  // 実行時間の計算

    	DOCA_LOG_INFO("Execution time: %f seconds\n", cpu_time_used);
		#endif

		DOCA_LOG_INFO("sleep 3");
		sleep(3);
		break;
	}



	/* Cleanup */

	cleanup_doca_apsh(apsh_ctx, sys);
	return DOCA_SUCCESS;
}
