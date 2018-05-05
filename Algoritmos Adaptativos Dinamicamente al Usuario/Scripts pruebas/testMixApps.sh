#!/bin/bash
# -*- ENCODING: UTF-8 -*-

function process_count_and_launch_time {

	launch_time=$(adb logcat -d | grep 'EscribanoTest1' | tail -1 | awk '{print $4}')
	normal_time=$(adb logcat -d | grep 'EscribanoTest3' | tail -1 | awk '{print $4}')
	adb logcat -c
	launch_time=$(echo $launch_time $aux | awk '{print $1 + $2}')
	echo "Launch Time: $launch_time"
	if [ $launch_time -le 2500 ]
	then
		total_process_count=$((10#$total_process_count + 1))
		var=$(adb shell dumpsys activity oom | grep -A 3 top-activity | grep lastPss | awk '{print $4}')
		IFS== read var1 var2 <<< $var
		echo "lastPss: $var2"
		if [ $var2 -eq 0 ]
		then
			new_process_count=$((10#$new_process_count + 1))
			echo "New process. Count=$new_process_count"
			launch_time_news=$(echo $launch_time_news $launch_time| awk '{print $1 + $2}')
			echo "New process. Launch time news=$launch_time_news"
		else
			active_process_count=$((10#$active_process_count + 1))
			echo "Active process. Count=$active_process_count"
			launch_time_actives=$(echo $launch_time_actives $launch_time| awk '{print $1 + $2}')
			echo "Active process. Launch time actives=$launch_time_actives"
		fi
	else
		normal_time=$(echo $normal_time $aux | awk '{print $1 + $2}')
		echo "Normal Time: $normal_time"
		if [ $normal_time -le 2500 ]
		then
			total_process_count=$((10#$total_process_count + 1))
			var=$(adb shell dumpsys activity oom | grep -A 3 top-activity | grep lastPss | awk '{print $4}')
			IFS== read var1 var2 <<< $var
			echo "lastPss: $var2"
			if [ $var2 -eq 0 ]
			then
				new_process_count=$((10#$new_process_count + 1))
				echo "New process. Count=$new_process_count"
				launch_time_news=$(echo $launch_time_news $normal_time| awk '{print $1 + $2}')
				echo "New process. Launch time news=$launch_time_news"
			else
				active_process_count=$((10#$active_process_count + 1))
				echo "Active process. Count=$active_process_count"
				launch_time_actives=$(echo $launch_time_actives $normal_time| awk '{print $1 + $2}')
				echo "Active process. Launch time actives=$launch_time_actives"
			fi
		else
			echo "Fail measure, not included"
			fail_measure=$((10#$fail_measure + 1))
		fi
	fi
}

function show_processes_and_services {

	adb shell cat /sys/module/lowmemorykiller/parameters/show_services_list
	adb shell cat /sys/module/lowmemorykiller/parameters/show_processes_list
	running=$(adb shell cat /sys/module/lowmemorykiller/parameters/test_running_count)
	running_count=$(echo $running_count $running | awk '{print $1 + $2}')
	count=$((10#$count + 1))
}


#INIT

	#ReuseApps
	new_process_count=0
	active_process_count=0
	total_process_count=0


	#LaunchTimes
	launch_time_news=0
	launch_time_actives=0
	aux=0
	fail_measure=0


	#RunningApps
	running_count=0
	count=0


	#KilledApps
	init_lmk_count=$(adb shell cat /sys/module/lowmemorykiller/parameters/test_lmk_count)
	echo "Init lmk count: $init_lmk_count"


	#PageFaults
	init_pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	init_pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	echo "Init pgfault: $init_pgfault"
	echo "Init pgmafult: $init_pgmafault"

#INIT



#EXECUTION
	adb shell input keyevent 26 #power button
	sleep 1
	adb shell input swipe 360 900 720 900 100 #unlock
	sleep 1
	adb shell input tap 360 200
	sleep 1
	show_processes_and_services
	echo "Init state"

#Block 0
	#Processes count and Launch Time calculation
	adb shell monkey -p com.king.candycrushsaga -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 15
	adb shell input tap 350 640
	sleep 2
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.grarak.kerneladiutor -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.rs.autokiller -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.chrome -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 0"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 0

#Block 1
	#Processes count and Launch Time calculation
	adb shell monkey -p com.codeaurora.fmradio -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.google.android.gm -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.grarak.kerneladiutor -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.opera.mini.native -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 15
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 1"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 1

#Block 2
	#Processes count and Launch Time calculation
	adb shell monkey -p com.twitter.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.codeaurora.fmradio -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.instagram.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 5
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.facebook.katana -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 2"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 2

#Block 3
	#Processes count and Launch Time calculation
	adb shell monkey -p com.codeaurora.fmradio -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.twitter.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.camerabq -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 2
	adb shell input tap 350 1250
	sleep 3
	adb shell input tap 350 1250

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.contacts -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 3"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 3

#Block 4
	#Processes count and Launch Time calculation
	adb shell monkey -p com.grarak.kerneladiutor -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.google.android.gm -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.facebook.katana -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.twitter.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 4"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 4

#Block 5
	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.chrome -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.instagram.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 5
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.email -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.camerabq -c android.intent.category.LAUNCHER 1
	sleep 2
	
	process_count_and_launch_time

	sleep 2
	adb shell input tap 350 1250
	sleep 3
	adb shell input tap 350 1250

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.instagram.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 5"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 5

#Block 6
	#Processes count and Launch Time calculation
	adb shell monkey -p com.facebook.katana -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.camerabq -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 2
	adb shell input tap 350 1250
	sleep 3
	adb shell input tap 350 1250

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish
	
	#Processes count and Launch Time calculation
	adb shell monkey -p com.whatsapp -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.contacts -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.twitter.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 6"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 6

#Block 7
	#Processes count and Launch Time calculation
	adb shell monkey -p com.king.candycrushsaga -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 15
	adb shell input tap 350 640
	sleep 2
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.chrome -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.grarak.kerneladiutor -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.rs.autokiller -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.supercell.clashofclans -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 20
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 7"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 7

#Block 8
	#Processes count and Launch Time calculation
	adb shell monkey -p com.king.candycrushsaga -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 15
	adb shell input tap 350 640
	sleep 2
	adb shell input keyevent 4 #back button
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.dropbox.android -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell am start -a android.intent.action.MAIN -n com.google.android.apps.plus/com.google.android.apps.photos.phone.PhotosLauncherActivity
	sleep 2

	process_count_and_launch_time

	sleep 5
	adb shell input keyevent 4 #back button
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.supercell.clashofclans -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 20
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish

	#Processes count and Launch Time calculation
	adb shell monkey -p com.android.chrome -c android.intent.category.LAUNCHER 1
	sleep 2

	process_count_and_launch_time

	sleep 10
	adb shell am start -a android.intent.action.MAIN -n com.android.launcher3/.Launcher
	sleep 2
	#Finish


	show_processes_and_services
	echo "Finish Block 8"

	#PageFaults
	pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	intermediate_pgfault=$(echo $pgfault $init_pgfault | awk '{print $1 - $2}')
	intermediate_pgmafault=$(echo $pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $intermediate_pgfault"
	echo "Main page faults: $intermediate_pgmafault"
	echo ""
#Block 8


#EXECUTION



#OBTAIN RESULTS

	#ReuseApps
	echo "News: $new_process_count"
	echo "Actives: $active_process_count"
	echo "Total: $total_process_count"

	success_rate=$(echo "scale=2; (100 * $active_process_count / $total_process_count)" | bc)
	failure_rate=$(echo "scale=2; (100 * $new_process_count / $total_process_count)" | bc)

	echo "Success: $success_rate %"
	echo "Failure: $failure_rate %"


	#LaunchTimes
	launch_time_average=$(echo "scale=2; (($launch_time_actives + $launch_time_news) / ($active_process_count + $new_process_count))" | bc)
	launch_time_news_average=$(echo "scale=2; $launch_time_news / $new_process_count" | bc)
	launch_time_actives_average=$(echo "scale=2; $launch_time_actives / $active_process_count" | bc)

	echo "Average Launch time: $launch_time_average"
	echo "Average Launch time news: $launch_time_news_average"
	echo "Average Launch time actives: $launch_time_actives_average"
	echo "Fail measures: $fail_measure"


	#RunningApps
	average_running=$(echo "scale=2; ($running_count / $count)" | bc)
	echo "Average running count: $average_running"


	#KilledApps
	finish_lmk_count=$(adb shell cat /sys/module/lowmemorykiller/parameters/test_lmk_count)
	lmk_count=$(echo $finish_lmk_count $init_lmk_count | awk '{print $1 - $2}')

	echo "Finish lmk count: $finish_lmk_count"
	echo "Apps killed during the test: $lmk_count"


	#PageFaults
	final_pgfault=$(adb shell grep pgfault /proc/vmstat | awk '{print $2}')
	final_pgmafault=$(adb shell grep pgmajfault /proc/vmstat | awk '{print $2}')

	echo "Final pgfault: $final_pgfault"
	echo "Final pgmafault: $final_pgmafault"

	total_pgfault=$(echo $final_pgfault $init_pgfault | awk '{print $1 - $2}')
	total_pgmafault=$(echo $final_pgmafault $init_pgmafault | awk '{print $1 - $2}')

	echo "Page faults: $total_pgfault"
	echo "Main page faults: $total_pgmafault"

#OBTAIN RESULTS

#FINAL RESULTS
	echo " "
	echo "FINAL RESULTS"
	echo "Success: $success_rate %"
	echo "Failure: $failure_rate %"

	echo "Average Launch time: $launch_time_average"
	echo "Average Launch time news: $launch_time_news_average"
	echo "Average Launch time actives: $launch_time_actives_average"
	echo "Fail measures: $fail_measure"

	echo "Average running count: $average_running"

	echo "Apps killed during the test: $lmk_count"

	echo "Page faults: $total_pgfault"
	echo "Main page faults: $total_pgmafault"