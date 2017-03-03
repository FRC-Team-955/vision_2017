#include <iostream>
#include <PegFinder.hpp>
#include <Networking.hpp>
#include <string>
#include <pugixml.hpp>
#include <Saving.hpp>
#include <SlidersTwo.hpp>
#include <RealSense.hpp>
#include <DummyCamera.hpp>
#include <opencv2/opencv.hpp>
#include <thread>
#include <pthread.h>
#include <vector>
#include <sys/stat.h>
#include <MultiBitEncoder.hpp>

using SaveEntry = std::unordered_map<std::string, int>;

std::unordered_map<std::string, SaveEntry*> saved_fields;

SaveEntry application_options;
SaveEntry imgproc_save_peg;
SaveEntry imgproc_save_boiler;
SaveEntry video_interface_save;
SaveEntry sliders_save_peg_limits;
SaveEntry sliders_save_boiler_limits;
SaveEntry sliders_save_peg;
SaveEntry sliders_save_boiler;

char* save_file_dir;
Saving* save_file;

Sliders* interface_peg;
Sliders* interface_boiler;

Realsense* sensor;
VideoInterface* dummy;

pugi::xml_document send_doc;

//char serial[11] = "2391000767"; //It's 10 chars long, but there's also the null char
char serial[11] = "2391011471"; //It's 10 chars long, but there's also the null char

void InitializeSaveFile () {
	//File saving fields
	sliders_save_peg  = {  
		{"hue_slider_lower"	,	179	}, 
		{"hue_slider_upper"	,	179	},
		{"sat_slider_lower"	,	256	},
		{"sat_slider_upper"	,	256	},
		{"val_slider_lower"	,	256	},
		{"val_slider_upper"	,	256	},
		{"area_slider"			,	0		}
	};

	sliders_save_peg_limits  = {  
		{"hue_slider_lower"	,	179	}, 
		{"hue_slider_upper"	,	179	},
		{"sat_slider_lower"	,	256	},
		{"sat_slider_upper"	,	256	},
		{"val_slider_lower"	,	256	},
		{"val_slider_upper"	,	256	},
		{"area_slider"			,	5000	}
	};

	sliders_save_boiler  = {  
		{"hue_slider_lower"	,	179	}, 
		{"hue_slider_upper"	,	179	},
		{"sat_slider_lower"	,	256	},
		{"sat_slider_upper"	,	256	},
		{"val_slider_lower"	,	256	},
		{"val_slider_upper"	,	256	},
		{"area_slider"			,	0		}
	};

	sliders_save_boiler_limits  = {  
		{"hue_slider_lower"	,	179	}, 
		{"hue_slider_upper"	,	179	},
		{"sat_slider_lower"	,	256	},
		{"sat_slider_upper"	,	256	},
		{"val_slider_lower"	,	256	},
		{"val_slider_upper"	,	256	},
		{"area_slider"			,	5000	}
	};

	video_interface_save = { 
		{"depth_width"			,	480	}, 
		{"depth_height"		,	360	}, 
		{"depth_framerate"	,	30		}, 
		{"bgr_width"			,	1920	}, 
		{"bgr_height"			,	1080	}, 
		{"bgr_framerate"		,	30		}, 
		{"exposure"				,	30		} 
	}; 

	imgproc_save_peg = {
		{"morph_open"				,	5		},
		{"morph_close"				,	5		},
		{"histogram_min"			,	1		}, 
		{"histogram_max"			,	50000	},
		{"histogram_percentile"	,	10		},
		{"camera_serial"			,	2391011471	}
	};

	imgproc_save_boiler = {
		{"morph_open"				,	5		},
		{"histogram_min"			,	1		}, 
		{"histogram_max"			,	50000	},
		{"histogram_percentile"	,	10		},
		{"camera_serial"			,	2391011471	}
	};

	application_options = {
		{"static_test"				,	0	}, 
		{"show_sliders_peg"		,	1	}, 
		{"show_sliders_boiler"	,	1	}, 
		{"show_rgb"					,	1	}, 
		{"show_depth"				,	1	}, 
		{"show_HSV"					,	1	}, 
		{"show_overlays"			,	1	}, 
	}; 

	saved_fields = {
		{"Sliders_Limits_Peg"		, &sliders_save_peg_limits		},
		{"Sliders_Limits_Boiler"	, &sliders_save_boiler_limits	},
		{"Sliders_Peg"					, &sliders_save_peg				},
		{"Sliders_Boiler"				, &sliders_save_boiler			},
		{"Imgproc_const_Peg"			, &imgproc_save_peg				},
		{"Imgproc_const_Boiler"		, &imgproc_save_boiler			},
		{"Application_Options"		, &application_options			},
		{"Sensor"						, &video_interface_save	 		}  
	};

	save_file = new Saving(save_file_dir, &saved_fields);

	if (!save_file->LoadJSON()) {
		std::cerr << "Save file does not exist. Creating defaults..." << std::endl;
		save_file->SaveJSON(); 
		std::cerr << "Finished creating defaults." << std::endl;
	}

}


//TODO: Move this stuff to it's own class or make it less icky somehow
pthread_mutex_t xml_mutex;
pthread_t xml_thread;
PegFinder* finder;
bool use_waitkey = false;
std::string out_string = "";

void* finder_thread (void* arg) {
	std::stringstream ss;
	while (true) {
		sensor->GrabFrames();	
		ss.str("");
		ss.clear();
		send_doc.reset();
		finder->ProcessFrame();
		send_doc.save(ss);

		pthread_mutex_lock(&xml_mutex);
		out_string = ss.str();
		pthread_mutex_unlock(&xml_mutex);

		if (use_waitkey) {cv::waitKey(10);};

	}
	return NULL;
}

void ServerMode() {
	std::stringstream ss;

	sensor = new Realsense( //TODO: Pass the entire video_interface_save object into the class, and use it locally there (Maybe)
			video_interface_save["depth_width"		], 
			video_interface_save["depth_height"		],
			video_interface_save["depth_framerate"	],
			video_interface_save["bgr_width"			],
			video_interface_save["bgr_height"		], 
			video_interface_save["bgr_framerate"	],
			serial
			); 

	use_waitkey = 
		!application_options["static_test"			] ||		
		!application_options["show_sliders_peg"	] ||
		!application_options["show_sliders_boiler"] ||
		!application_options["show_rgb"				] || 
		!application_options["show_depth"			] ||
		!application_options["show_HSV"				] || 
		!application_options["show_overlays"		] ;


	Networking::Server* serv = new Networking::Server(5806);			

	//cv::Mat display_out;
	//display_out = *sensor->bgrmatCV;

	finder = new PegFinder(
			sensor							, 
			&sliders_save_peg				,	
			&sliders_save_peg_limits	,	
			&imgproc_save_peg				,	
			&application_options			,	
			&video_interface_save		,
			&send_doc
			);

	finder->ProcessFrame();
	send_doc.save(ss);

	pthread_mutex_init(&xml_mutex, NULL);
	pthread_create(&xml_thread, NULL, &finder_thread, NULL);

	while(true) {
		std::cerr << "Waiting for client connection on port " << 5806 << std::endl;
		serv->WaitForClientConnection();
		while (serv->GetNetState()) {
			std::cerr << serv->WaitForClientMessage();

			pthread_mutex_lock(&xml_mutex);
			serv->SendClientMessage(out_string.c_str());
			pthread_mutex_unlock(&xml_mutex);

		}
		std::cerr << "Connection stopped. Uh oh." << std::endl;
	}

}

void TestStatic(char* rgb_directory, char* depth_directory) {
	dummy = new DummyCamera(
			rgb_directory,
			depth_directory,
			video_interface_save["depth_width"	], 
			video_interface_save["depth_height"	],
			video_interface_save["bgr_width"		],
			video_interface_save["bgr_height"	] 
			); 

	PegFinder* finder = new PegFinder(
			dummy								, 
			&sliders_save_peg				,	
			&sliders_save_peg_limits	,	
			&imgproc_save_peg				,	
			&application_options			,	
			&video_interface_save		,
			&send_doc
			);


	while(true) {
		dummy->GrabFrames();
		finder->ProcessFrame();
		send_doc.save(std::cout);
		cv::waitKey(10);
	}
}


Mat small;
void TestLive() {
	std::cout << imgproc_save_peg["camera_serial"] << std::endl;
	//char serial[11] = "2391011471"; //It's 10 chars long, but there's also the null char

	system("v4l2-ctl --set-ctrl exposure_auto=1 -d 2");
	system(("v4l2-ctl --set-ctrl exposure_absolute=" + std::to_string(video_interface_save["exposure"]) + " -d 2").c_str());

	sensor = new Realsense( //TODO: Pass the entire video_interface_save object into the class, and use it locally there (Maybe)
			video_interface_save["depth_width"		], 
			video_interface_save["depth_height"		],
			video_interface_save["depth_framerate"	],
			video_interface_save["bgr_width"			],
			video_interface_save["bgr_height"		], 
			video_interface_save["bgr_framerate"	],
			serial
			); 

	//sensor->SetColorExposure(video_interface_save["exposure"]);

	PegFinder* finder = new PegFinder(
			sensor							, 
			&sliders_save_peg				,	
			&sliders_save_peg_limits	,	
			&imgproc_save_peg				,	
			&application_options			,	
			&video_interface_save		,
			&send_doc
			);

	//TODO: Move this to main() 
	bool use_waitkey = 
		!application_options["static_test"			] ||		
		!application_options["show_sliders_peg"	] ||
		!application_options["show_sliders_boiler"] ||
		!application_options["show_rgb"				] || 
		!application_options["show_depth"			] ||
		!application_options["show_HSV"				] || 
		!application_options["show_overlays"		] ;

	std::vector<uchar> buff;

	//Networking::Server* serv = new Networking::Server(2345);
	//serv->WaitForClientConnection();
	sensor->GrabFrames(); //Initialize largedepthCV
	VideoWriter writer;
	//writer.open(getDateFileName(), CV_FOURCC('M', 'J', 'P', 'G'), 30, sensor->largeDepthCV->size(), true);
	Mat img8c3 (
			video_interface_save["bgr_height"],
			video_interface_save["bgr_width"], 
			CV_8UC3);

	//MultiBitEncoder* encoder = new MultiBitEncoder(1, &img8c3, sensor->largeDepthCV);

	while(true) {
		sensor->GrabFrames();
		finder->ProcessFrame(); //TODO: Make this less self-contained!
		send_doc.save(std::cout);
		cv::waitKey(1);
	}
}

/*
std::string getDateFileName () {
	auto t = std::time(nullptr);
	auto tm = *std::localtime(&t);
	std::stringstream ss; //Dumb hack
	ss << std::put_time(&tm, "%a-%m-%d-%Y-%H-%M-%S");
	return ss.str();
}
*/

int main (int argc, char** argv) {
	//Command args
	if (argc < 2) {
		std::cerr << "Usage: " <<
			"\n\t " << argv[0] << " <Settings.json>" << std::endl;
		return -1;
	} 

	save_file_dir = argv[1];

	InitializeSaveFile();

	if (application_options["static_test"] == 1) {
		if (argc < 4) {
			std::cerr << "Test mode active, requires 2 additional arguements." << 
				"\nUsage" << 
				"\n\t" << argv[0] << " <Settings.json> <RGB.png> <Depth.exr>" << std::endl;
			return -1;
		}
	}

	Sliders* interface_peg = new Sliders("Peg_Finder_Sliders", &sliders_save_peg, &sliders_save_peg_limits, save_file); 
	Sliders* interface_boiler = new Sliders("Boiler_Finder_Sliders", &sliders_save_boiler, &sliders_save_boiler_limits, save_file); 

	if (application_options["show_sliders_peg"]) {
		interface_peg->InitializeSliders();
	}

	if (application_options["show_sliders_boiler"]) {
		interface_boiler->InitializeSliders();
	}

	interface_peg->UpdateSliders();
	interface_boiler->UpdateSliders();

	switch (application_options["static_test"]) {
		case 2:
			TestLive();
		case 1:
			TestStatic(argv[2], argv[3]);
		default:
			ServerMode();
			break;
	}

}


