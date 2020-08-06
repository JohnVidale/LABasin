function PGASD = att2(M,R,Ft)
%-------------------------------------------------------------------------%
%                                                                         %
%        Strong Ground-Motion Attenuation Relationship (PGA)              %
%                                                                         %
% Reference: Idriss (1991) reported in Idriss (1993)                      %
%-------------------------------------------------------------------------%

%(Step 1) %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%% Set Coefficient %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

a10            = -0.150; 
a20            = -0.050;
a11            = 2.261; 
a21            = 3.477;
a12            = -0.083; 
a22            = -0.284;
b10            = 0; 
b20            = 0;
b11            = 1.602; 
b21            = 2.475;
b12            = -0.142; 
b22            = -0.286;
h              = 10;
a              = 0.2;

if Ft          == 1
F              = 0;   % For strike-slip fault
else if Ft     == 2
F              = 0.5; % For oblique fault
else if Ft     == 3
F              = 1;   % For reverse fault
else if Ft     == 4
F              = 0;   % For Normal fault
end
end
end
end

%(Step 2) %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%% Calculate mean PGA and Standard Deviation %%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
    
if M<=6
PGA           = log((exp((a10+exp(a11+a12*M))+(b10-exp(b11+b12*M))*log(R+20))+(a*F))*981); % in unit "ln(gal)" (1 gal=0.01 m/s2)
else
PGA           = log((exp((a20+exp(a21+a22*M))+(b20-exp(b21+b22*M))*log(R+20))+(a*F))*981); % in unit "ln(gal)" (1 gal=0.01 m/s2)
end; 

if M < 7.25
SD            = 1.39-0.14*M;
else
SD            = 0.38;
end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
PGASD          = [PGA SD];
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%