package com.dava.framework;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.GestureDetector;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup.LayoutParams;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

import com.bda.controller.ControllerListener;
import com.bda.controller.StateEvent;

public class JNIGLSurfaceView extends GLSurfaceView
{
	private JNIRenderer mRenderer = null;
	// we have to add flag to distinguish second call to onResume()
	// during Activity.onResume or Activity.onWindowsFocusChanged(focus)
	private boolean alreadyResumed = false;

	private native void nativeOnInput(int action, int source, int groupSize, ArrayList< InputRunnable.InputEvent > activeInputs, ArrayList< InputRunnable.InputEvent > allInputs);
	private native void nativeOnKeyDown(int keyCode);
	private native void nativeOnKeyUp(int keyCode);
	
	MOGAListener mogaListener = null;

	boolean[] pressedKeys = new boolean[KeyEvent.getMaxKeyCode() + 1];

	public int lastDoubleActionIdx = -1;
	
	class DoubleTapListener extends GestureDetector.SimpleOnGestureListener{
		JNIGLSurfaceView glview;
		
		DoubleTapListener(JNIGLSurfaceView view) {
			this.glview = view;
		}
		
		@Override
		public boolean onDoubleTap(MotionEvent e) {
			lastDoubleActionIdx = e.getActionIndex();
			
			glview.queueEvent(new InputRunnable(e, 2));
			return true;
		}
	}
	
	GestureDetector doubleTapDetector = null;

	public JNIGLSurfaceView(Context context) 
	{
		super(context);
		Init();
	}

	public JNIGLSurfaceView(Context context, AttributeSet attrs)
	{
		super(context, attrs);
		Init();
	}

	private void Init()
	{
		this.getHolder().setFormat(PixelFormat.TRANSLUCENT);

		if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.HONEYCOMB)
		{
			setPreserveEGLContextOnPause(true);
		}
		setEGLContextFactory(new JNIContextFactory());
		setEGLConfigChooser(new JNIConfigChooser());

		mRenderer = new JNIRenderer();
		setRenderer(mRenderer);
		setRenderMode(RENDERMODE_CONTINUOUSLY);
		
		mogaListener = new MOGAListener(this);
		
		setDebugFlags(0);
		
		doubleTapDetector = new GestureDetector(JNIActivity.GetActivity(), new DoubleTapListener(this));
	}
	
    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        // Fix lag when text field lost focus, but keyboard not closed yet. 
        outAttrs.imeOptions = JNITextField.GetLastKeyboardIMEOptions();
        outAttrs.inputType = JNITextField.GetLastKeyboardInputType();
        return super.onCreateInputConnection(outAttrs);
    }
    
	@Override
	protected void onSizeChanged(int w, int h, int oldw, int oldh) {
		//YZ rewrite size parameter from fill parent to fixed size
		LayoutParams params = getLayoutParams();
		params.height = h;
		params.width = w;
		super.onSizeChanged(w, h, oldw, oldh);
	}
	
    @Override
    public void onPause() {
        Log.d(JNIConst.LOG_TAG, "Activity JNIGLSurfaceView onPause");
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        queueEvent(new Runnable() {
            public void run() {
                mRenderer.OnPause();
            }
        });
        // destroy eglCondext(or unbind), eglScreen, eglSurface
        super.onPause();
        alreadyResumed = false;
    }

    @Override
    public void onResume() {
        Log.d(JNIConst.LOG_TAG, "Activity JNIGLSurfaceView onResume");
        if (!alreadyResumed) {
            // first call parent to restore eglContext
            super.onResume();
            queueEvent(new Runnable() {
                public void run() {
                    mRenderer.OnResume();
                }
            });
            setRenderMode(RENDERMODE_CONTINUOUSLY);
            alreadyResumed = true;
        }
    };

	Map<Integer, Integer> tIdMap = new HashMap<Integer, Integer>();
	int nexttId = 1;
	
	public class InputRunnable implements Runnable
	{
		public class InputEvent
		{
			int tid;
			float x;
			float y;
			int tapCount;
			double time;

			InputEvent(int tid, float x, float y, double time)
			{
				this.tid = tid;
				this.x = x;
				this.y = y;
				this.tapCount = 1;
				this.time = time;
			}
			
			InputEvent(int tid, float x, float y, int tapCount, double time)
			{
				this.tid = tid;
				this.x = x;
				this.y = y;
				this.tapCount = tapCount;
				this.time = time;
			}
		}
		
		final int [] axis = {
				MotionEvent.AXIS_X,
				MotionEvent.AXIS_Y,
				MotionEvent.AXIS_Z,
				MotionEvent.AXIS_RX,
				MotionEvent.AXIS_RY,
				MotionEvent.AXIS_RZ,
				MotionEvent.AXIS_LTRIGGER,
				MotionEvent.AXIS_RTRIGGER,
				MotionEvent.AXIS_HAT_X,
				MotionEvent.AXIS_HAT_Y,
		};

		ArrayList<InputEvent> activeEvents;
		ArrayList<InputEvent> allEvents;

		int action;
		int source;
		int groupSize;

		public InputRunnable(final android.view.MotionEvent event, final int tapCount)
		{
			allEvents = new ArrayList<InputEvent>();

			action = event.getActionMasked();
			source = event.getSource();

			final int historySize = event.getHistorySize();
			final int pointerCount = event.getPointerCount();

			for (int historyStep = 0; historyStep < historySize; historyStep++) {
				for (int i = 0; i < pointerCount; i++) {
					if ((source & InputDevice.SOURCE_CLASS_POINTER) > 0) {
						int pointerId = event.getPointerId(i);
						InputEvent ev = new InputEvent(pointerId, event.getHistoricalX(i, historyStep), event.getHistoricalY(i, historyStep), tapCount, event.getHistoricalEventTime(historyStep));

						allEvents.add(ev);
					}
					if((source & InputDevice.SOURCE_CLASS_JOYSTICK) > 0) {
						for (int a = 0; a < axis.length; ++a) {
							InputEvent ev = new InputEvent(a + 1, event.getHistoricalAxisValue(axis[a], i, historyStep), 0, tapCount, event.getHistoricalEventTime(historyStep));

							allEvents.add(ev);
						}
					}
				}
			}

			for (int i = 0; i < pointerCount; i++) {
				if ((source & InputDevice.SOURCE_CLASS_POINTER) > 0) {
					int pointerId = event.getPointerId(i);
					InputEvent ev = new InputEvent(pointerId, event.getX(i), event.getY(i), tapCount, event.getEventTime());

					allEvents.add(ev);
				}
				if((source & InputDevice.SOURCE_CLASS_JOYSTICK) > 0) {
					for (int a = 0; a < axis.length; ++a) {
						InputEvent ev = new InputEvent(a + 1, event.getAxisValue(axis[a], i), 0, tapCount, event.getEventTime());
						allEvents.add(ev);
					}
				}
			}

			if (action == MotionEvent.ACTION_MOVE) {
				activeEvents = allEvents;
				groupSize = event.getPointerCount();
			} else {
				activeEvents = new ArrayList<InputEvent>();

				int actionIdx = event.getActionIndex();
				assert(actionIdx <= event.getPointerCount());
				
				final int pointerId = event.getPointerId(actionIdx);

				InputEvent ev = new InputEvent(pointerId, event.getX(actionIdx), event.getY(actionIdx), tapCount, event.getEventTime());
				allEvents.add(ev);
				activeEvents.add(ev);
				groupSize = event.getPointerCount() + 1; // only ACTION_MOVE events can have history, so in this case there will be only one group
			}
    	}
    	public InputRunnable(final com.bda.controller.MotionEvent event)
    	{
    		action = MotionEvent.ACTION_MOVE;
    		allEvents = new ArrayList<InputEvent>();
    		source = InputDevice.SOURCE_CLASS_JOYSTICK;
        	int pointerCount = event.getPointerCount();
	    	for (int i = 0; i < pointerCount; ++i)
	    	{
	    		//InputEvent::id corresponds to axis id from UIEvent::eJoystickAxisID
	        	allEvents.add(new InputEvent(1, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_X, i), 0, event.getEventTime()));
	        	allEvents.add(new InputEvent(2, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Y, i), 0, event.getEventTime()));
	        	allEvents.add(new InputEvent(3, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_Z, i), 0, event.getEventTime()));
	        	allEvents.add(new InputEvent(6, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RZ, i), 0, event.getEventTime()));
	        	allEvents.add(new InputEvent(7, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_LTRIGGER, i), 0, event.getEventTime()));
	        	allEvents.add(new InputEvent(8, event.getAxisValue(com.bda.controller.MotionEvent.AXIS_RTRIGGER, i), 0, event.getEventTime()));
    		}
    		activeEvents = allEvents;
    		groupSize = event.getPointerCount();
    	}

		@Override
		public void run() {
			nativeOnInput(action, source, groupSize, activeEvents, allEvents);
		}
    }

    class KeyInputRunnable implements Runnable {
    	int keyCode;
    	public KeyInputRunnable(int keyCode) {
    		this.keyCode = keyCode;
    	}
    	
    	@Override
    	public void run() {
    		nativeOnKeyDown(keyCode);
    	}
    }
    
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
    	// Check keyCode value for pressedKeys array limit
    	if(keyCode >= pressedKeys.length)
    	{
    		return super.onKeyDown(keyCode, event);
    	}
    	
    	if(pressedKeys[keyCode] == false)
    		queueEvent(new KeyInputRunnable(keyCode));
    	pressedKeys[keyCode] = true;
    	
    	if (event.isSystem())
    		return super.onKeyDown(keyCode, event);
    	else
    		return true;
    }
    
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
    	// Check keyCode value for pressedKeys array limit
    	if(keyCode >= pressedKeys.length)
    	{
    		return super.onKeyUp(keyCode, event);
    	}
    	
    	pressedKeys[keyCode] = false;
        nativeOnKeyUp(keyCode);
    	return super.onKeyUp(keyCode, event);
    }
    
    @Override
    public boolean onTouchEvent(MotionEvent event) 
    {
        boolean isDoubleTap = doubleTapDetector.onTouchEvent(event);
        if (lastDoubleActionIdx >= 0 &&
        	lastDoubleActionIdx == event.getActionIndex() &&
        	event.getAction() == MotionEvent.ACTION_UP) {
        	lastDoubleActionIdx = -1;
        	queueEvent(new InputRunnable(event, 2));
        	isDoubleTap = true;
        }
        if (!isDoubleTap)
            queueEvent(new InputRunnable(event, 1));
        return true;
    }
    
    @Override
    public boolean onGenericMotionEvent(MotionEvent event)
    {
    	queueEvent(new InputRunnable(event, 1));
    	return true;
    }
    
    class MOGAListener implements ControllerListener
    {
    	GLSurfaceView parent = null;
    	
    	MOGAListener(GLSurfaceView parent)
    	{
    		this.parent = parent;
    	}
    	
		@Override
		public void onKeyEvent(com.bda.controller.KeyEvent event)
		{
			int keyCode = event.getKeyCode();
			if(event.getAction() == com.bda.controller.KeyEvent.ACTION_DOWN)
			{
		    	if(pressedKeys[keyCode] == false)
		    		parent.queueEvent(new KeyInputRunnable(keyCode));
		    	pressedKeys[keyCode] = true;
			}
			else if(event.getAction() == com.bda.controller.KeyEvent.ACTION_UP)
			{
		    	pressedKeys[keyCode] = false;
		        nativeOnKeyUp(keyCode);
			}
		}
		@Override
		public void onMotionEvent(com.bda.controller.MotionEvent event)
		{
			parent.queueEvent(new InputRunnable(event));
		}
		@Override
		public void onStateEvent(StateEvent event)
		{
			
		}
    }
}
