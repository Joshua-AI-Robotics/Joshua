import tkinter as tk
import numpy as np
from scipy.spatial.transform import Rotation

# --- 1. Robot Definition using Simple Python Classes ---

class Vector3:
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x = x
        self.y = y
        self.z = z

class KinematicLink:
    def __init__(self, name, translation, rotation_axis, lower_limit, upper_limit):
        self.name = name
        self.translation = translation
        self.rotation_axis = rotation_axis
        self.lower_limit = lower_limit
        self.upper_limit = upper_limit

class PhysicalLayout:
    def __init__(self, links):
        self.links = links

def get_robot_layout():
    layout = PhysicalLayout(links=[
        KinematicLink(
            name="joint_1_base_rotation",
            translation=Vector3(x=0.0, y=0.0, z=0.12),
            rotation_axis=Vector3(x=0.0, y=0.0, z=1.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        ),
        KinematicLink(
            name="joint_2_shoulder_pitch",
            translation=Vector3(x=0.0, y=0.0, z=0.05),
            rotation_axis=Vector3(x=0.0, y=1.0, z=0.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        ),
        KinematicLink(
            name="joint_3_elbow_pitch",
            translation=Vector3(x=0.25, y=0.0, z=0.0),
            rotation_axis=Vector3(x=0.0, y=1.0, z=0.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        ),
        KinematicLink(
            name="joint_4_wrist_roll",
            translation=Vector3(x=0.22, y=0.0, z=0.0),
            rotation_axis=Vector3(x=1.0, y=0.0, z=0.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        ),
        KinematicLink(
            name="joint_5_wrist_pitch",
            translation=Vector3(x=0.08, y=0.0, z=0.0),
            rotation_axis=Vector3(x=0.0, y=1.0, z=0.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        ),
        KinematicLink(
            name="joint_6_flange_roll",
            translation=Vector3(x=0.06, y=0.0, z=0.0),
            rotation_axis=Vector3(x=1.0, y=0.0, z=0.0),
            lower_limit=-3.14159,
            upper_limit=3.14159
        )
    ])
    return layout

# --- 2. Forward Kinematics Calculation ---
def forward_kinematics(layout, joint_angles):
    current_position = np.array([0.0, 0.0, 0.0])
    current_rotation = Rotation.identity()
    joint_positions = [current_position.copy()]
    for i, link in enumerate(layout.links):
        translation_vec = np.array([link.translation.x, link.translation.y, link.translation.z])
        current_position += current_rotation.apply(translation_vec)
        axis = np.array([link.rotation_axis.x, link.rotation_axis.y, link.rotation_axis.z])
        angle = joint_angles[i]
        joint_rotation = Rotation.from_rotvec(angle * axis)
        current_rotation = current_rotation * joint_rotation
        joint_positions.append(current_position.copy())
    return np.array(joint_positions)

# --- 3. Enhanced 3D to 2D Projection with Interactive Controls ---
def project_points(points3d, width, height, view_elev=30, view_azim=45, zoom=300, pan_x=0, pan_y=0):
    # Enhanced perspective projection with interactive controls
    elev = np.deg2rad(view_elev)
    azim = np.deg2rad(view_azim)
    
    # Rotation matrix for view
    R_elev = np.array([
        [1, 0, 0],
        [0, np.cos(elev), -np.sin(elev)],
        [0, np.sin(elev), np.cos(elev)]
    ])
    R_azim = np.array([
        [np.cos(azim), -np.sin(azim), 0],
        [np.sin(azim), np.cos(azim), 0],
        [0, 0, 1]
    ])
    R = R_azim @ R_elev
    
    # Apply rotation and scale
    pts = (R @ points3d.T).T
    x = pts[:, 0] * zoom + width // 2 + pan_x
    y = -pts[:, 1] * zoom + height // 2 + pan_y
    return np.stack([x, y], axis=1)

# --- 4. Enhanced Tkinter Visualization with Interactive Controls ---
class RobotArmVisualizer(tk.Tk):
    def __init__(self, layout):
        super().__init__()
        self.title("3D Robot Arm Visualizer - Interactive (Tkinter)")
        self.geometry("1400x900")  # Much larger window
        
        self.layout = layout
        self.n_joints = len(layout.links)
        self.joint_angles = [0.0] * self.n_joints
        
        # View control parameters
        self.view_elev = 30.0
        self.view_azim = 45.0
        self.zoom = 300.0
        self.pan_x = 0.0
        self.pan_y = 0.0
        
        # Mouse interaction variables
        self.last_mouse_x = 0
        self.last_mouse_y = 0
        self.mouse_dragging = False
        
        # Canvas dimensions
        self.canvas_width = 800
        self.canvas_height = 700
        
        self._build_ui()
        self._draw_arm()
        self._bind_mouse_events()

    def _build_ui(self):
        # Main frame
        main_frame = tk.Frame(self)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Left panel for controls
        left_panel = tk.Frame(main_frame, width=300)
        left_panel.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))
        
        # Canvas for drawing (larger)
        canvas_frame = tk.Frame(main_frame)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        self.canvas = tk.Canvas(canvas_frame, width=self.canvas_width, height=self.canvas_height, 
                               bg='white', relief=tk.RAISED, bd=2)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        
        # Control sections
        self._build_joint_controls(left_panel)
        self._build_view_controls(left_panel)
        self._build_info_panel(left_panel)

    def _build_joint_controls(self, parent):
        # Joint controls section
        joint_frame = tk.LabelFrame(parent, text="Joint Controls", padx=10, pady=5)
        joint_frame.pack(fill=tk.X, pady=(0, 10))
        
        self.scales = []
        for i, link in enumerate(self.layout.links):
            # Create a frame for each joint control
            joint_control = tk.Frame(joint_frame)
            joint_control.pack(fill=tk.X, pady=2)
            
            # Label
            label = tk.Label(joint_control, text=f"{i+1}. {link.name}", anchor='w', font=('Arial', 9))
            label.pack(anchor='w')
            
            # Scale slider
            scale = tk.Scale(joint_control, from_=link.lower_limit, to=link.upper_limit, 
                           resolution=0.01, orient=tk.HORIZONTAL, length=250,
                           command=lambda val, idx=i: self._on_slider(idx, val))
            scale.set(0.0)
            scale.pack(fill=tk.X, pady=(0, 5))
            self.scales.append(scale)

    def _build_view_controls(self, parent):
        # View controls section
        view_frame = tk.LabelFrame(parent, text="View Controls", padx=10, pady=5)
        view_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Elevation control
        tk.Label(view_frame, text="Elevation:").pack(anchor='w')
        self.elev_scale = tk.Scale(view_frame, from_=-90, to=90, resolution=1, 
                                 orient=tk.HORIZONTAL, length=250,
                                 command=lambda val: self._on_view_change('elev', val))
        self.elev_scale.set(self.view_elev)
        self.elev_scale.pack(fill=tk.X, pady=(0, 5))
        
        # Azimuth control
        tk.Label(view_frame, text="Azimuth:").pack(anchor='w')
        self.azim_scale = tk.Scale(view_frame, from_=0, to=360, resolution=1, 
                                 orient=tk.HORIZONTAL, length=250,
                                 command=lambda val: self._on_view_change('azim', val))
        self.azim_scale.set(self.view_azim)
        self.azim_scale.pack(fill=tk.X, pady=(0, 5))
        
        # Zoom control
        tk.Label(view_frame, text="Zoom:").pack(anchor='w')
        self.zoom_scale = tk.Scale(view_frame, from_=100, to=800, resolution=10, 
                                 orient=tk.HORIZONTAL, length=250,
                                 command=lambda val: self._on_view_change('zoom', val))
        self.zoom_scale.set(self.zoom)
        self.zoom_scale.pack(fill=tk.X, pady=(0, 5))
        
        # Reset view button
        reset_btn = tk.Button(view_frame, text="Reset View", command=self._reset_view)
        reset_btn.pack(pady=5)

    def _build_info_panel(self, parent):
        # Information panel
        info_frame = tk.LabelFrame(parent, text="Instructions", padx=10, pady=5)
        info_frame.pack(fill=tk.X, pady=(0, 10))
        
        instructions = [
            "Mouse Controls:",
            "• Left click + drag: Rotate view",
            "• Right click + drag: Pan view", 
            "• Mouse wheel: Zoom in/out",
            "",
            "Keyboard:",
            "• R: Reset view",
            "• Space: Reset all joints"
        ]
        
        for instruction in instructions:
            label = tk.Label(info_frame, text=instruction, anchor='w', font=('Arial', 8))
            label.pack(anchor='w')

    def _bind_mouse_events(self):
        self.canvas.bind('<Button-1>', self._on_mouse_down)
        self.canvas.bind('<B1-Motion>', self._on_mouse_drag)
        self.canvas.bind('<ButtonRelease-1>', self._on_mouse_up)
        self.canvas.bind('<Button-3>', self._on_right_mouse_down)
        self.canvas.bind('<B3-Motion>', self._on_right_mouse_drag)
        self.canvas.bind('<MouseWheel>', self._on_mouse_wheel)
        self.canvas.bind('<Button-4>', self._on_mouse_wheel)  # Linux scroll up
        self.canvas.bind('<Button-5>', self._on_mouse_wheel)  # Linux scroll down
        
        # Keyboard events
        self.bind('<Key-r>', lambda e: self._reset_view())
        self.bind('<space>', lambda e: self._reset_joints())
        self.bind('<Key-R>', lambda e: self._reset_view())

    def _on_mouse_down(self, event):
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y
        self.mouse_dragging = True

    def _on_mouse_drag(self, event):
        if self.mouse_dragging:
            dx = event.x - self.last_mouse_x
            dy = event.y - self.last_mouse_y
            
            # Update azimuth and elevation
            self.view_azim += dx * 0.5
            self.view_elev += dy * 0.5
            
            # Clamp elevation
            self.view_elev = max(-90, min(90, self.view_elev))
            
            # Update sliders
            self.azim_scale.set(self.view_azim)
            self.elev_scale.set(self.view_elev)
            
            self.last_mouse_x = event.x
            self.last_mouse_y = event.y
            self._draw_arm()

    def _on_mouse_up(self, event):
        self.mouse_dragging = False

    def _on_right_mouse_down(self, event):
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y

    def _on_right_mouse_drag(self, event):
        dx = event.x - self.last_mouse_x
        dy = event.y - self.last_mouse_y
        
        # Pan the view
        self.pan_x += dx
        self.pan_y += dy
        
        self.last_mouse_x = event.x
        self.last_mouse_y = event.y
        self._draw_arm()

    def _on_mouse_wheel(self, event):
        # Handle mouse wheel for zoom
        if event.num == 4 or (hasattr(event, 'delta') and event.delta > 0):  # Scroll up
            self.zoom *= 1.1
        elif event.num == 5 or (hasattr(event, 'delta') and event.delta < 0):  # Scroll down
            self.zoom /= 1.1
        
        # Clamp zoom
        self.zoom = max(50, min(1000, self.zoom))
        self.zoom_scale.set(self.zoom)
        self._draw_arm()

    def _on_view_change(self, param, val):
        val = float(val)
        if param == 'elev':
            self.view_elev = val
        elif param == 'azim':
            self.view_azim = val
        elif param == 'zoom':
            self.zoom = val
        self._draw_arm()

    def _reset_view(self):
        self.view_elev = 30.0
        self.view_azim = 45.0
        self.zoom = 300.0
        self.pan_x = 0.0
        self.pan_y = 0.0
        
        self.elev_scale.set(self.view_elev)
        self.azim_scale.set(self.view_azim)
        self.zoom_scale.set(self.zoom)
        self._draw_arm()

    def _reset_joints(self):
        for i, scale in enumerate(self.scales):
            scale.set(0.0)
            self.joint_angles[i] = 0.0
        self._draw_arm()

    def _on_slider(self, idx, val):
        self.joint_angles[idx] = float(val)
        self._draw_arm()

    def _draw_arm(self):
        self.canvas.delete('all')
        
        # Draw coordinate axes for reference
        self._draw_coordinate_axes()
        
        # Calculate and draw robot arm
        points3d = forward_kinematics(self.layout, self.joint_angles)
        points2d = project_points(points3d, self.canvas_width, self.canvas_height,
                                self.view_elev, self.view_azim, self.zoom, self.pan_x, self.pan_y)
        
        # Draw links with better styling
        for i in range(len(points2d) - 1):
            x0, y0 = points2d[i]
            x1, y1 = points2d[i + 1]
            
            # Draw link line
            self.canvas.create_line(x0, y0, x1, y1, fill='#2E86AB', width=6)
            
            # Draw joint circle
            self.canvas.create_oval(x0-8, y0-8, x0+8, y0+8, fill='#A23B72', outline='#2E86AB', width=2)
            
            # Add joint labels
            if i < len(self.layout.links):
                self.canvas.create_text(x0+15, y0-15, text=f"J{i+1}", 
                                      font=('Arial', 10, 'bold'), fill='#2E86AB')
        
        # Draw end effector
        x_end, y_end = points2d[-1]
        self.canvas.create_oval(x_end-10, y_end-10, x_end+10, y_end+10, 
                              fill='#F18F01', outline='#2E86AB', width=2)
        self.canvas.create_text(x_end+15, y_end-15, text="EE", 
                              font=('Arial', 10, 'bold'), fill='#F18F01')
        
        # Draw base
        x_base, y_base = points2d[0]
        self.canvas.create_oval(x_base-12, y_base-12, x_base+12, y_base+12, 
                              fill='#C73E1D', outline='#2E86AB', width=2)
        self.canvas.create_text(x_base+15, y_base-15, text="BASE", 
                              font=('Arial', 10, 'bold'), fill='#C73E1D')

    def _draw_coordinate_axes(self):
        # Draw coordinate axes for reference
        origin = np.array([0.0, 0.0, 0.0])
        x_axis = np.array([0.2, 0.0, 0.0])
        y_axis = np.array([0.0, 0.2, 0.0])
        z_axis = np.array([0.0, 0.0, 0.2])
        
        axes_3d = np.array([origin, x_axis, origin, y_axis, origin, z_axis])
        axes_2d = project_points(axes_3d, self.canvas_width, self.canvas_height,
                               self.view_elev, self.view_azim, self.zoom, self.pan_x, self.pan_y)
        
        # Draw axes with colors
        self.canvas.create_line(axes_2d[0][0], axes_2d[0][1], axes_2d[1][0], axes_2d[1][1], 
                              fill='red', width=3)  # X-axis
        self.canvas.create_line(axes_2d[2][0], axes_2d[2][1], axes_2d[3][0], axes_2d[3][1], 
                              fill='green', width=3)  # Y-axis
        self.canvas.create_line(axes_2d[4][0], axes_2d[4][1], axes_2d[5][0], axes_2d[5][1], 
                              fill='blue', width=3)  # Z-axis

if __name__ == '__main__':
    try:
        robot_layout = get_robot_layout()
        app = RobotArmVisualizer(robot_layout)
        app.mainloop()
    except ImportError:
        print("Error: Required libraries not found.")
        print("Please install them using: pip install numpy scipy")
    except Exception as e:
        print(f"An error occurred: {e}")