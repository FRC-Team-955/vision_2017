#include <SplineCalc.hpp>

SplineCalc::SplineCalc(Settings::spline_generator_options* options) {
	this->options = options;

#if GENERATE_PLOT
	save_center_display.open("/tmp/center_display.csv");
	save_left_display.open("/tmp/left_display.csv");
	save_right_display.open("/tmp/right_display.csv");
	save_points_display.open("/tmp/points_display.csv"); 
#endif
	}

cv::Point2f SplineCalc::RationalVecConv(std::vector<ts::rational>* input) {
	return cv::Point2f(input->at(0), input->at(1));
}

cv::Point2f SplineCalc::RationalVecConv(std::vector<ts::rational> input) {
	return cv::Point2f(input.at(0), input.at(1));
}

//TODO: Make this less shitty
float SplineCalc::ReallyCrappyRamp (float i) {
	float first_point = 0.1f;
	float second_point = 0.9f;
	if (i > 0.0f && i < first_point) {
		return (i / first_point) * options->max_velocity;
	} else if (i >= first_point && i <= second_point) {
		return options->max_velocity;
	} else if (i >= second_point && i < 0.999) {
		return (1.0f - ((i - second_point) / (1.0f - second_point))) * options->max_velocity;
	} else {
		return 0.01f;
	}
}

void SplineCalc::CalcPaths(std::vector<motion_plan_result>* left_tracks, std::vector<motion_plan_result>* right_tracks, float goal_slope, cv::Point2f goal_position) {
	//Create spline and control points
	ts::BSpline spline(3, 2, 6, TS_CLAMPED);

	std::vector<ts::rational> ctrlp = spline.ctrlp();
	//First three are for the robot's position and vector
	ctrlp[0] =  options->robot_origin.x;  				// x0
	ctrlp[1] =  options->robot_origin.y;  				// y0

	ctrlp[2] =  options->robot_origin.x;  				// x1
	ctrlp[3] =  options->robot_origin.y + options->control_point_distance * 1.0f; 				// y1

	ctrlp[4] =  options->robot_origin.x;  				// x2
	ctrlp[5] =  options->robot_origin.y + (options->control_point_distance * 2.0f);  	// y2

	//Last three are for the goal's position and vector
	goal_position += options->end_offset;
	ctrlp[10] =  goal_position.x;  			// x3
	ctrlp[11] =  goal_position.y;  			// y3

	cv::Point2f first_outcrop = MoveAlongLine(goal_slope > 0, options->control_point_distance * 1.0f, NegativeReciprocal(goal_slope), goal_position);

	ctrlp[8] = first_outcrop.x;  				// x4
	ctrlp[9] = first_outcrop.y;  				// y4

	cv::Point2f second_outcrop = MoveAlongLine(goal_slope > 0, options->control_point_distance * 2.0f, NegativeReciprocal(goal_slope), goal_position);

	ctrlp[6] =  second_outcrop.x;  			// x5
	ctrlp[7] =  second_outcrop.y;  			// y5

	spline.setCtrlp(ctrlp);

	//Derive the curve, as we use the slope at each point (really it's perpendicular) to find the point for each track
	ts::BSpline derivation = spline.derive();

	float compounded_left = 0.0f;
	float compounded_right = 0.0f;

	cv::Point2f normal_left_last (0.0f, 0.0f);
	cv::Point2f normal_right_last (0.0f, 0.0f); 
	cv::Point2f pos_center_last (0.0f, 0.0f); 

	float center_line_last_distance = 0.0f;

	float i = 0.0f;
	cv::Point2f spline_end_point = RationalVecConv(spline.evaluate(1.0f).result());

	bool first_point = true;
	float max_accel = (options->max_acceleration / 60.0f) * (options->delta_time / 1000);
	float max_travel = (options->max_velocity / 60.0f) * (options->delta_time / 1000);
	do  {
		if (center_line_last_distance < max_travel) { //Accelerate Slowly
			center_line_last_distance += max_accel;
			if(center_line_last_distance > max_travel) {
				center_line_last_distance = max_travel;
			}
		}

		i = SplineChopRecurse(center_line_last_distance, i, 1.0f, 0.001f, &spline, 0); //Find the next multiple of the max_travel along this spline

		cv::Point2f pos_center = RationalVecConv(spline.evaluate(i).result());
		cv::Point2f spline_center = RationalVecConv(spline.evaluate(i).result());
		cv::Point2f spline_derive = RationalVecConv(derivation.evaluate(i).result());

		/* 
		 * Create new points that start out at the point along the spline,
		 * and extend out along the normal the length of the offset from 
		 * the center of the drive base
		 */
		cv::Point2f normal_left = MoveAlongLine(spline_derive.y < 0,
				options->wheel_seperation,
				NegativeReciprocal(spline_derive.y / spline_derive.x),
				spline_center
				);

		cv::Point2f normal_right = MoveAlongLine(spline_derive.y > 0,
				options->wheel_seperation,
				NegativeReciprocal(spline_derive.y / spline_derive.x),
				spline_center
				);

		if (first_point) { //Distances should remain zero at the first point
			normal_right_last = normal_right;
			normal_left_last = normal_left;
			pos_center_last = pos_center;
			first_point = false;
		}

		float travel_left = MiscImgproc::PointDistance(normal_left, normal_left_last);
		float travel_right = MiscImgproc::PointDistance(normal_right, normal_right_last);
		float travel_center = MiscImgproc::PointDistance(pos_center, pos_center_last);

#if GENERATE_PLOT
		if (!plot_constructed) {
			save_left_display 	<< normal_left.x 		<< ", " 	<< normal_left.y		<< ", " << (travel_left 	/ options->delta_time) * 60 * 1000 << std::endl;
			save_right_display 	<< normal_right.x 	<< ", " 	<< normal_right.y		<< ", " << (travel_right 	/ options->delta_time) * 60 * 1000 << std::endl;
			save_center_display 	<< spline_center.x 	<< "," 	<< spline_center.y 	<< ", " << (travel_center 	/ options->delta_time) * 60 * 1000 << std::endl;
		}
#endif
		compounded_left += travel_left;
		compounded_right += travel_right;

		left_tracks->push_back(
				motion_plan_result(
					compounded_left,
					(travel_left / options->delta_time) * 60 * 1000,
					options->delta_time
					)
				);

		right_tracks->push_back(
				motion_plan_result(
					compounded_right,
					(travel_right / options->delta_time) * 60 * 1000,
					options->delta_time
					)
				);

		normal_right_last = normal_right;
		normal_left_last = normal_left;
		pos_center_last = pos_center;

	} while (MiscImgproc::PointDistance(pos_center_last, spline_end_point) > max_travel && i < 1.0f);

#if GENERATE_PLOT
	if (!plot_constructed) {
		for (int i = 0; i < ctrlp.size(); i+=2) {
			save_points_display << ctrlp[i] << "," << ctrlp[i+1] << std::endl;
		}
		plot_constructed = true;
	}
#endif

}

float SplineCalc::SplineChopRecurse (float max_travel, float start, float end, float tolerance, ts::BSpline* spline, int depth) {
	depth++;
	if(start - end == 0) {
		return end;
	}
	if (depth > options->max_recursion_depth) {
		return end;
	}

	float distance_to_end = MiscImgproc::PointDistance(
			RationalVecConv(spline->evaluate(start).result()),
			RationalVecConv(spline->evaluate(end).result())
			);
	float midpoint = ((end - start) / 2.0f) + start;
	float distance_to_midpt = MiscImgproc::PointDistance(
			RationalVecConv(spline->evaluate(start).result()),
			RationalVecConv(spline->evaluate(midpoint).result())
			);

	bool midpoint_range_within_tol = ToleranceCheck(distance_to_midpt, max_travel, tolerance);
	bool end_range_within_tol = ToleranceCheck(distance_to_midpt, max_travel, tolerance);
	if (midpoint_range_within_tol || end_range_within_tol) {
		if (midpoint_range_within_tol) {
			return midpoint;
		} else {
			return end;
		}
	} else {
		if (max_travel > distance_to_midpt && max_travel < distance_to_end) {
			return SplineChopRecurse(max_travel - (distance_to_midpt), midpoint, end, tolerance, spline, depth);
		} else {
			return SplineChopRecurse(max_travel, start, midpoint, tolerance, spline, depth);
		} 
	} 
	return end;
}
