#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

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

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
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
	//Lane where the car starts
	int next_lane = -1;

	//Change lane signal
	bool change_lane = false;
	bool changing_lane = false;

	//Reference velocity in mph
	double ref_vel = 0;
	double min_speed = ref_vel;
	bool too_close = false;

  h.onMessage([&ref_vel, &min_speed, &too_close, &change_lane, &changing_lane, &next_lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
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

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

			//Size of what is left from the previous path sent to the simulator
			int previous_size = previous_path_x.size();

			//find the actual lane
			int lane = 0;
			if (car_d > 0 && car_d < 4) lane = 0;
			else if (car_d > 4 && car_d < 8) lane = 1;
			else if (car_d > 8 && car_d < 12) lane = 2;
			
			if (next_lane == lane){
				change_lane = false;
				changing_lane = false;
			}
			
			//Adjust the velocity						
			for (int i = 0; i < sensor_fusion.size(); i++){
				//check if there is a car in the same lane
				float d = sensor_fusion[i][6];
				if ((d < 4*(lane +1) && d > 4*lane) || ((d < 4*(next_lane +1) && d > 4*next_lane) && next_lane != -1)){
					double vx = sensor_fusion[i][3];
					double vy = sensor_fusion[i][4];
					double speed = sqrt(vx*vx + vy*vy);
					
					double check_car_s = sensor_fusion[i][5];

					check_car_s += (double)previous_size * 0.02 * speed/2.24;

					if ((check_car_s > car_s) && ((check_car_s - car_s) < 30) ){
						too_close = true;
						change_lane = true;
						i  = sensor_fusion.size();
						min_speed = speed;
						//ref_vel = speed*2.24;
					}else{
						too_close = false;
						min_speed = ref_vel;
					}
				}	
			}
									 
			//Change lane if there is a car ahead						
			if (change_lane and !changing_lane){
				double final_cost = min_speed;
				for (int check_lane = 0; check_lane <= 2; check_lane++){
					double cost = 2*ref_vel;
					if(check_lane != lane){
					
					bool safe_lane_change = true;
					for (int i = 0; i < sensor_fusion.size(); i++){
						//check if there is a car in the same lane
						float d = sensor_fusion[i][6];
						if (d < 4*(check_lane +1) && d > 4*check_lane){
							double vx = sensor_fusion[i][3];
							double vy = sensor_fusion[i][4];
							double speed = sqrt(vx*vx + vy*vy);
							double check_car_s = sensor_fusion[i][5];

							check_car_s += (double)previous_size * 0.02 * speed/2.24;

							//Check if there is a car at the same level
							if (abs(check_car_s - car_s) <25){
								safe_lane_change = false;
							}

							//Check if there is a car ahead but has a lower velocity
							if ((check_car_s > car_s) && ((check_car_s - car_s) < 30) && (speed*2.24 <= ref_vel)){
								safe_lane_change = false;
							}

							//Check if there is a car coming from behind with a higher velocity
							if ((check_car_s < car_s) && ((car_s - check_car_s ) < 25) && (speed*2.24 > ref_vel)){
								safe_lane_change = false;
							}

							if(safe_lane_change ){
								cost = speed*2.24;
							}

						}	
					}
					if (safe_lane_change && abs(check_lane - lane)==1){
						next_lane = check_lane;
						changing_lane = true;
						final_cost = cost;
						check_lane = 3;
					}								
				}
				}
			}

			cout<<"\nlane: "<<lane<<" next_lane: "<<next_lane<<" too_close: "<<too_close<<" change_lane: "<<change_lane;
			if (too_close && ref_vel > min_speed && ref_vel > 30  && !changing_lane){
				ref_vel  -= 0.24;						
			}else if(ref_vel < 49.7){
				ref_vel += 0.24;
			}
			
			//Next path points
			vector<double> next_x_vals;
				vector<double> next_y_vals;

			//Create a list of evenly spaced (30 meters) waypoints (x,y) that we will interpolate in the spline function
			vector<double> px;
			vector<double> py;

			//Create car reference coordinates : x, y and yaw
			double ref_x = car_x;
			double ref_y = car_y;
			double ref_yaw = deg2rad(car_yaw);

			//Define two points that make the path tagent to the car
			double previous_car_x;
			double previous_car_y;

			//If previous size is almost empty, use the car as a starting reference, else use the previous path's end waypoints
			if(previous_size < 2 ){

				//Use the two points to make the path tangent to the car
				previous_car_x = car_x - cos(ref_yaw);
				previous_car_y = car_y - sin(ref_yaw);
				
			}else{
				//Assign the previous path's end waypoints to the reference coordinates
				
				ref_x = previous_path_x[previous_size-1];
				ref_y = previous_path_y[previous_size-1];

				previous_car_x = previous_path_x[previous_size-2];
				previous_car_y = previous_path_y[previous_size-2];
				ref_yaw = atan2(ref_y - previous_car_y, ref_x - previous_car_x);
				
			}
			//Add the coordinates to the spline waypoints
			px.push_back(previous_car_x);
			px.push_back(ref_x);
			py.push_back(previous_car_y);
			py.push_back(ref_y);
			
			//Add 3 evenly spaced waypoints of 30 meters ahead of the car (in Frenet Coordiantes) to the spline waypoints
			vector<double> next_waypoint_1, next_waypoint_2, next_waypoint_3;
			vector<double> ref_frenet = getFrenet(ref_x, ref_y, ref_yaw, map_waypoints_x, map_waypoints_y);
			if(next_lane != -1 && next_lane != lane){
				
				next_waypoint_1 = getXY(ref_frenet[0] + 30, 2+4 * next_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				next_waypoint_2 = getXY(ref_frenet[0] + 60, 2+4 * next_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				next_waypoint_3 = getXY(ref_frenet[0] + 90, 2+4 * next_lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
			}else{
				next_waypoint_1 = getXY(ref_frenet[0] + 15, 2+4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				next_waypoint_1 = getXY(ref_frenet[0] + 30, 2+4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				next_waypoint_2 = getXY(ref_frenet[0] + 60, 2+4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				next_waypoint_3 = getXY(ref_frenet[0] + 90, 2+4 * lane, map_waypoints_s, map_waypoints_x, map_waypoints_y);
			}						

			px.push_back(next_waypoint_1[0]);
			px.push_back(next_waypoint_2[0]);
			px.push_back(next_waypoint_3[0]);

			py.push_back(next_waypoint_1[1]);
			py.push_back(next_waypoint_2[1]);
			py.push_back(next_waypoint_3[1]);

			//Change the coordinates with respect to the car coordiante system so that we can compute the spline
			for (int i=0; i < px.size(); i++){

				double shift_x = px[i]-ref_x;
				double shift_y = py[i]-ref_y;

				px[i] = (shift_x)*cos(ref_yaw) + (shift_y)*sin(ref_yaw);
				py[i] = (shift_y)*cos(ref_yaw) - (shift_x)*sin(ref_yaw);

			}

			//Create a spline
			tk::spline s;

			//set the points to the spline
			s.set_points(px, py);

			//Add the previous path points still not reached by the car to the list of the next points that we will send to the simulator
			
			for (int i=0; i < previous_size ; i++){					
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
			}
			

			//Calculate the points from the spline that we will insert into the list of the next points taking into account the desired velocity
			double x_final = 90.0;
			double y_final = s(x_final);
			double target_distance = distance(x_final, y_final, 0, 0);

			//Compute the number of waypoints
			double N = target_distance*2.24/(0.02*ref_vel);

			//Fill the rest of the list of the next waypoints
			double x_add_on = 0;

			for (int i=1; i<=50-previous_path_x.size(); i++){
				double x = x_add_on + x_final/N;
				double y = s(x);

				x_add_on = x;

				//Convert back to map coordinates
				double tx = x*cos(ref_yaw)-y*sin(ref_yaw)+ref_x;
				double ty = x*sin(ref_yaw)+y*cos(ref_yaw)+ref_y;
				
				next_x_vals.push_back(tx);
				next_y_vals.push_back(ty);

			}  

          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

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
