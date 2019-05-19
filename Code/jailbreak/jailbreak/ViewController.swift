//
//  ViewController.swift
//  jailbreak
//
//  Created by Swapnil on 11/2/17.
//  Copyright © 2017 SwapnilMe. All rights reserved.
//

import UIKit

class ViewController: UIViewController {
    
    override func viewDidLoad() {
        super.viewDidLoad()
        DispatchQueue.main.async(execute: { () -> Void in
           jail();
        })
    }
    
    
    override func didReceiveMemoryWarning() {
        super.didReceiveMemoryWarning()
        // Dispose of any resources that can be recreated.
    }

    
}

