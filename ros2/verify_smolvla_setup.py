#!/usr/bin/env python3
"""
Verification script for SmolVLA inference setup.

This script checks if all required dependencies are installed
and accessible for running the SmolVLA inference node.

Usage:
    python3 ros2/verify_smolvla_setup.py
"""

import sys
from typing import Tuple

def check_import(module_name: str, package_name: str = None) -> Tuple[bool, str]:
    """
    Try to import a module and return success status and message.
    
    Args:
        module_name: Name of the module to import
        package_name: Name of the package (for display), defaults to module_name
    
    Returns:
        Tuple of (success: bool, message: str)
    """
    if package_name is None:
        package_name = module_name
    
    try:
        __import__(module_name)
        return True, f"✅ {package_name} is installed"
    except ImportError as e:
        return False, f"❌ {package_name} is NOT installed: {str(e)}"

def check_cuda() -> Tuple[bool, str]:
    """Check if CUDA is available for PyTorch."""
    try:
        import torch
        if torch.cuda.is_available():
            device_count = torch.cuda.device_count()
            device_name = torch.cuda.get_device_name(0) if device_count > 0 else "Unknown"
            return True, f"✅ CUDA is available: {device_count} device(s), {device_name}"
        else:
            return False, "⚠️  CUDA is NOT available (CPU mode will be used)"
    except ImportError:
        return False, "❌ Cannot check CUDA (torch not installed)"

def check_transformers_version() -> Tuple[bool, str]:
    """Check if transformers version is sufficient."""
    try:
        import transformers
        version = transformers.__version__
        # Check if version is >= 4.30.0 (rough requirement for VLA models)
        major, minor = map(int, version.split('.')[:2])
        if major >= 4 and minor >= 30:
            return True, f"✅ transformers version {version} is sufficient"
        else:
            return False, f"⚠️  transformers version {version} may be too old (need >= 4.30.0)"
    except ImportError:
        return False, "❌ transformers not installed"
    except Exception as e:
        return False, f"⚠️  Could not check transformers version: {str(e)}"

def check_model_access() -> Tuple[bool, str]:
    """Check if we can access HuggingFace models."""
    try:
        from huggingface_hub import HfApi
        api = HfApi()
        # Try to get model info (doesn't download the model)
        try:
            api.model_info("HuggingFaceH4/SmolVLA-1.7B")
            return True, "✅ Can access HuggingFace models (internet connection OK)"
        except Exception as e:
            return False, f"⚠️  Cannot access HuggingFace models: {str(e)}\n   (You can still use local checkpoints)"
    except ImportError:
        return False, "⚠️  huggingface_hub not installed (needed for downloading models)"

def main():
    """Run all verification checks."""
    print("=" * 70)
    print("SmolVLA Inference Setup Verification")
    print("=" * 70)
    print()
    
    # Core dependencies
    print("📦 Checking Core Dependencies:")
    print("-" * 70)
    
    checks = [
        ("torch", "PyTorch"),
        ("transformers", "Transformers"),
        ("PIL", "Pillow"),
        ("numpy", "NumPy"),
    ]
    
    all_passed = True
    for module, package in checks:
        success, message = check_import(module, package)
        print(f"  {message}")
        if not success:
            all_passed = False
    
    print()
    
    # Additional checks
    print("🔧 Checking Additional Features:")
    print("-" * 70)
    
    # CUDA check
    cuda_success, cuda_message = check_cuda()
    print(f"  {cuda_message}")
    
    # Transformers version
    tf_success, tf_message = check_transformers_version()
    print(f"  {tf_message}")
    
    # Model access
    model_success, model_message = check_model_access()
    print(f"  {model_message}")
    
    print()
    
    # ROS2 dependencies (optional)
    print("🤖 Checking ROS2 Dependencies (will be available via Bazel):")
    print("-" * 70)
    
    ros_checks = [
        ("rclpy", "rclpy"),
        ("sensor_msgs", "sensor_msgs"),
        ("std_msgs", "std_msgs"),
    ]
    
    for module, package in ros_checks:
        success, message = check_import(module, package)
        # ROS2 imports are expected to fail in dev environment
        if not success:
            print(f"  ℹ️  {package} not in current Python path (this is OK, Bazel will provide it)")
        else:
            print(f"  {message}")
    
    print()
    print("=" * 70)
    
    # Summary
    if all_passed:
        print("✅ All core dependencies are installed!")
        print()
        if cuda_success:
            print("🚀 GPU acceleration is available - you're ready for real-time inference!")
        else:
            print("⚠️  No GPU detected - inference will run on CPU (slower)")
        print()
        print("Next steps:")
        print("  1. Build: bazel build //ros2:smolvla_inference")
        print("  2. Configure: edit config/config_preset/so100_smolvla_inference.pbtxt")
        print("  3. Run: bazel run //ros2:smolvla_inference -- --config <your_config>")
        print()
        print("📖 See ros2/SMOLVLA_USAGE.md for detailed usage instructions")
    else:
        print("❌ Some dependencies are missing!")
        print()
        print("To install missing dependencies:")
        print("  pip install torch transformers pillow numpy huggingface-hub accelerate")
        print()
        print("Or use the project requirements:")
        print("  pip install -r requirements.txt")
    
    print("=" * 70)
    
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())

