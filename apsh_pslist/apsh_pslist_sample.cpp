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

// non-blocking comm
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#define BUFFER_SIZE 1024
#define PORT 12345

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

void printTree(std::map<unsigned int, struct Node>& tree) {
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

int validate(unsigned int current_pid, unsigned int euid, std::map<unsigned int, struct Node>& tree){
	int detected_count = 0;
	DOCA_LOG_INFO("VALIDATION: PID: %u, EUID: %u",current_pid,euid);
	for(const unsigned int& next_pid : tree[current_pid].child_pids){
		detected_count = validate(next_pid, euid, tree);
	}
	if(tree[current_pid].euid != euid){
		sig_stop_process(current_pid);
		return detected_count + 1;
	}
	return detected_count;
}


void add_stepd(){

}

void del_stepd(){
    
}

int create_server_fd(){
	int server_fd;
	int opt = 1;
	struct sockaddr_in address;
	// ソケットの作成
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        DOCA_LOG_ERR("socket failed");
        exit(EXIT_FAILURE);
    }

    // ソケットオプションの設定
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        DOCA_LOG_ERR("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // アドレスの設定
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // すべてのインターフェースで受け入れ
    address.sin_port = htons(PORT);

    // ソケットにアドレスをバインド
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        DOCA_LOG_ERR("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 接続の待ち受け
    if (listen(server_fd, 3) < 0) {
        DOCA_LOG_ERR("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // ソケットをノンブロッキングに設定
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

	return server_fd;
}


void receive_data_from_SPANK(int server_fd, unsigned int **data, unsigned int *num_data){
	int new_socket, valread, max_sd;
    struct sockaddr_in address;
    char *token;
    int addrlen = sizeof(address);
	char buffer[BUFFER_SIZE] = {0};
    struct timeval timeout;  // タイムアウト用の構造体

    fd_set readfds;
	// =========== while ===============
    // fd_set を初期化
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);
    max_sd = server_fd;
    // タイムアウトをゼロに設定（ノンブロッキングで即座に戻る）
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    // select() を使用してソケットを監視
    int activity = select(max_sd + 1, &readfds, NULL, NULL, &timeout);
    if ((activity < 0) && (errno != EINTR)) {
        DOCA_LOG_ERR("select error");
    }
    // 新しい接続がある場合
    if (FD_ISSET(server_fd, &readfds)) {
		// アドレスの設定
    	address.sin_family = AF_INET;
    	address.sin_addr.s_addr = INADDR_ANY;  // すべてのインターフェースで受け入れ
    	address.sin_port = htons(PORT);
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                DOCA_LOG_ERR("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }
        } else {
            // 受信バッファをクリア
            memset(buffer, 0, BUFFER_SIZE);
            valread = read(new_socket, buffer, BUFFER_SIZE);
            if (valread > 0) {
                // 受信したデータを解析して動的に格納
                unsigned int *received_data = NULL;
                unsigned int count = 0;

                token = strtok(buffer, " ");
                while (token != NULL) {
                    count++;
                    received_data = static_cast<unsigned int*>(realloc(received_data, count * sizeof(unsigned int)));
                    if (received_data == NULL) {
                        DOCA_LOG_ERR("realloc failed");
                        close(new_socket);
                        return;
                    }
                    received_data[count - 1] = (unsigned int)atoi(token);
                    token = strtok(NULL, " ");
                }

                // 受信したデータとデータ数を設定
                *data = received_data;
                *num_data = count;
                for (unsigned int i = 0; i < count; i++) {
                    DOCA_LOG_INFO("%u ", received_data[i]);
                }
            }
            close(new_socket);  // ソケットを閉じて次の接続を待つ
        }
    }
}

void update_stepd(std::map<unsigned int,unsigned int>& stepd, unsigned int *data, unsigned int num_data){
	if(num_data == 1){
		stepd.erase(data[0]);
	}
	// dataは必ず1セット2つもしくは1つで送られる。socketは別で作っているので、それ以上になることはない
	else {
		stepd[data[0]] = data[1];
	}
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


	int server_fd = create_server_fd();
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
	std::map<unsigned int, struct Node> tree; // {pid:node}
	unsigned int pid = 0;
	unsigned int euid = 0;
	unsigned int ppid = 0;
	int flag = 0;
	while(1){

		// get stepd info and create tree for each
		unsigned int *data = NULL;
        unsigned int num_data = 0;
        receive_data_from_SPANK(server_fd,&data,&num_data);
		if(num_data) update_stepd(euid_of_stepd,data,num_data);
		
		
		result = doca_apsh_processes_get(sys, &pslist, &num_processes);
        	if (result != DOCA_SUCCESS) {
                	DOCA_LOG_ERR("Failed to create the process list");
                	cleanup_doca_apsh(apsh_ctx, sys);
        	        return result;
	        }
        // create tree
		tree.clear();
		for (i = 0; i < num_processes; ++i) {
			pid = doca_apsh_process_info_get(pslist[i], DOCA_APSH_PROCESS_PID);
			euid = doca_apsh_process_info_get(pslist[i],DOCA_APSH_PROCESS_LINUX_UID);
			ppid = doca_apsh_process_info_get(pslist[i],DOCA_APSH_PROCESS_PPID);

			if (ppid < 3)continue;
			
			struct Node node;
			node.pid = pid;
			node.ppid = ppid;
			node.euid = euid;
			tree[pid] = node;
			tree[ppid].child_pids.insert(pid);
		}

		
		//printTree(tree);

		// test for each stepd
        int is_detected = 0;
		for(std::map<unsigned int, unsigned int>::iterator it = euid_of_stepd.begin(); it != euid_of_stepd.end(); ++it){
			pid = it->first;
			euid = it->second;
			DOCA_LOG_INFO("%d %d",pid,euid);
			tree[pid].euid = euid;
			is_detected = validate(pid,euid,tree);
			if (is_detected){
				sig_stop_process(pid);
			}
		}


		/* Print some attributes of the processes */
		if(flag == 0){
			printProcess(pslist,num_processes);
			flag=1;
		}
		doca_apsh_processes_free(pslist);

		#ifdef DEBUG
    	end = clock();  // 終了時刻の記録
    	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;  // 実行時間の計算

    	//DOCA_LOG_INFO("Execution time: %f seconds\n", cpu_time_used);
		#endif

	//	DOCA_LOG_INFO("sleep 3");
	//	sleep(3);
//		break;
	}



	/* Cleanup */

	cleanup_doca_apsh(apsh_ctx, sys);
	return DOCA_SUCCESS;
}
