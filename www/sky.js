var Sky = {}


//Retrieves the light direction
Sky.get_sun_dir = function()
{
	return [0, 1, 0];
}

Sky.get_basis = function()
{
	var n = Sky.get_sun_dir,
		u = [0, 0, 1];
		v = cross(n, u);
		
	v /= Math.sqrt(dot(v, v));
		
	return [n, u, v];
}


//Returns the color of the sunlight
Sky.get_sun_color = function()
{
	return [0.79, 0.81,  0.85];
}

//Draws the sky background
Sky.draw_bg = function()
{
}
