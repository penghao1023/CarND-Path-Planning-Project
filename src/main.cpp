#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"



// for convenience
using nlohmann::json;
using std::string;
using std::vector;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

 

  double ref_vel = 0.0;
  int lane = 1;
  int lanes_num = 3;
  string cur_state = "KL";
  double delta_goal = 1.0;
  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy,&ref_vel,&cur_state,&lane, &lanes_num,&max_s,&delta_goal]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];
	  int prev_size = previous_path_x.size();
	  
	  if (prev_size >0) {
	    car_s = end_path_s;
	  }	  
	  

	  vector <double> costs;
	  vector <string> states;
	  vector <vector <double>> kinematics; 
	  double delta_s = max_s - car_s;
	  // transition part:
	  // calculate the best state and the corresponding target speed and lane given the pose, state, 
	  // kinematic, sensor_fusion of the car
	  
	  if (abs(delta_s) < 10) {
	    delta_goal = 0.0;
	  }
	  
	  if (delta_goal == 0.0) {
	    std::cout << " Complete all the highway!" << std::endl;
	  }
 	  	  
	  vector <string> possb_states = successor_states(cur_state, lane, lanes_num);
	  for (vector <string>::iterator it = possb_states.begin(); it!=possb_states.end();++it) {
	    vector <double> cur_trajactory = generate_trajectory(car_speed, car_s, *it, 49.5, lane, prev_size, sensor_fusion);
	    //std::cout << cur_trajactory[0] << " " << cur_trajactory[1] << " " << cur_trajactory[2] << " " << cur_trajactory[3] << std::endl;
	    double ineffi_cost = inefficient_cost(cur_trajactory);
	    //std::cout << "inefficient_cost:" << ineffi_cost << std::endl;
	    double gdc_cost = goal_distance_cost(cur_trajactory);
	    //std::cout << "goal_distance_cost:" << gdc_cost << std::endl;	
	    double w1 = 1.0; //weight for the ineffi_cost
	    double w2 = 4.5; //weight for the lanechange_cost
	    double cost = w1*ineffi_cost+w2*gdc_cost;
	    costs.push_back(cost);
	    kinematics.push_back(cur_trajactory);
	    states.push_back(*it);
	  }
	  double min_cost = 999999.9;
	  vector <double> best_kinematic;
	  string best_state;
	  for (int i=0; i<costs.size();i++) {
	    vector <double> kinematic = kinematics[i];
	    double cost = costs[i];
	    if (cost<min_cost) {
	      min_cost = cost;
	      best_kinematic = kinematic;
	      best_state = states[i];
	    }
	  }
	  int best_lane = int(best_kinematic[3])+lane;
	  double best_speed = best_kinematic[2];
	  
	  lane = best_lane;
	  cur_state = best_state;
	  
	  //std::cout << "current_state:" << cur_state << std::endl;
	  //std::cout << "best_lane:" << best_lane << std::endl;
	  //std::cout << "best_speed:" << best_speed << std::endl;
	  //std::cout << "best_state:" << best_state << std::endl;
	  //std::cout << "current_s: " << car_s << std::endl;
	  //std::cout << "delta_s: " << delta_s << std::endl;	  
	 
	  
	  // a two stage deceleration is set according to the distance to the front car 
	  if (ref_vel > best_speed) {
	    if (best_kinematic[5] > 20) {
	      ref_vel -= 0.50;
	    } else if (best_kinematic[5] <20)
	    ref_vel -= 0.90;
	      
	  }
	  else if (ref_vel <48.5) {
	    ref_vel += 0.70;
	    //std::cout << ref_vel << std::endl;
	  }
	  


          // generate the trjactory by spline liabrary for the car to follow
          // given the target speed and target lane command obtained above.
          json msgJson;


          /**
           * TODO: define a path made up of (x,y) points that the car will visit
           *   sequentially every .02 seconds
           */
	  // Create a list of widely spaced (x,y) waypoints, evenly spaced at 30cm
	  // Later we will interoplate these waypoints with a spline and fill it in with more points 
	  
	  //Define the actual points we will use for the planner
	  
	  vector<double> ptsx;
	  vector<double> ptsy;
	  
	  // reference x,y,yaw states
	  // either we will reference the starting point as where the car is or at the previous paths end point
	  double ref_x = car_x;
	  double ref_y = car_y;
	  double ref_yaw = deg2rad(car_yaw);
	  
	  //if previous size is almost empty, use the car as starting reference
	  if (prev_size <2) {
	    //use two points that make the path tangent to the car
	    double prev_car_x = car_x - cos(car_yaw);
	    double prev_car_y = car_y - sin(car_yaw);
	    
	    ptsx.push_back(prev_car_x);
	    ptsx.push_back(car_x);
	    
	    ptsy.push_back(prev_car_y);
	    ptsy.push_back(car_y);
	  }
	  // use the previous path's end point as starting reference
	  else {
	    
	    //Redefine reference state as previous path end point
	    ref_x = previous_path_x[prev_size - 1];
	    ref_y = previous_path_y[prev_size - 1];
	    
	    double ref_x_prev = previous_path_x[prev_size-2];
	    double ref_y_prev = previous_path_y[prev_size-2];
	    ref_yaw = atan2(ref_y-ref_y_prev,ref_x-ref_x_prev);
	    
	    //Use two points that make the path tangent to the previous path's end point
	    ptsx.push_back(ref_x_prev);
	    ptsx.push_back(ref_x);
	    
	    ptsy.push_back(ref_y_prev);
	    ptsy.push_back(ref_y);
	 
	  }
	  
	  //In Frenet add evenly 35m spaced points ahead of the starting reference
	  vector<double> next_wp0 = getXY(car_s+35,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
	  vector<double> next_wp1 = getXY(car_s+70,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
	  vector<double> next_wp2 = getXY(car_s+105,(2+4*lane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
	  
	  ptsx.push_back(next_wp0[0]);
	  ptsx.push_back(next_wp1[0]);
	  ptsx.push_back(next_wp2[0]);
	  
	  ptsy.push_back(next_wp0[1]);
	  ptsy.push_back(next_wp1[1]);
	  ptsy.push_back(next_wp2[1]);
	  
	  for (int i=0; i<ptsx.size();i++) {
	    
	    //shift car reference angle to 0 degrees
	    double shift_x = ptsx[i]-ref_x;
	    double shift_y = ptsy[i]-ref_y;
	    
	    ptsx[i] = (shift_x*cos(0-ref_yaw)-shift_y*sin(0-ref_yaw));
	    ptsy[i] = (shift_x*sin(0-ref_yaw)+shift_y*cos(0-ref_yaw));
	    
	  }
	  
	  // create a spline
	  tk::spline s;
	  
	  //set (x,y) points to the spline
	  s.set_points(ptsx,ptsy);
	  
	  vector<double> next_x_vals;
          vector<double> next_y_vals;
	  
	  // Start with all of the previous path points from last time
	  for(int i=0; i<previous_path_x.size();i++) {
	    next_x_vals.push_back(previous_path_x[i]);
	    next_y_vals.push_back(previous_path_y[i]);
	  }
	  
	  //Calculate how to break up spline points so that we travel at our desired reference velocity
	  double target_x = 30.0;
	  double target_y = s(target_x);
	  double target_dist = sqrt((target_x)*(target_x)+(target_y)*(target_y));
	  
	  double x_add_on = 0;
	  
	  //Fill up the rest of our path planner after filling it with previous points, here we will always output 50 points
	  for (int i=1; i <= 50-previous_path_x.size(); i++) {
	    
	    double N = (target_dist/(.02*ref_vel/2.24));
	    double x_point = x_add_on+(target_x)/N;
	    double y_point = s(x_point);
	    
	    x_add_on = x_point;
	    
	    double x_ref = x_point;
	    double y_ref = y_point;
	    
	    // rotate back to normal after rotating it earlier
	    x_point = (x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw));
	    y_point = (x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw));
	    
	    x_point += ref_x;
	    y_point += ref_y;
	    
	    next_x_vals.push_back(x_point);
	    next_y_vals.push_back(y_point);
	  }
	  
	  


  
	  //END


          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}